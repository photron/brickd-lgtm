/*
 * daemonlib
 * Copyright (C) 2012, 2014, 2016-2017, 2019-2020 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * log.c: Logging specific functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdbool.h>
#include <string.h>

#include "log.h"

#include "config.h"
#include "threads.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

typedef struct {
	bool included;
	char source_name[64];
	uint32_t groups;
} LogDebugFilter;

#define MAX_OUTPUT_SIZE (5 * 1024 * 1024) // bytes
#define ROTATE_COUNTDOWN 50

static Mutex _mutex; // protects writing to _output, _output_size, _rotate and _rotate_countdown
static LogLevel _level = LOG_LEVEL_INFO;
static IO *_output = NULL;
static int64_t _output_size = -1; // tracks size if output is rotatable
static LogRotateFunction _rotate = NULL;
static int _rotate_countdown = 0;
static bool _debug_override = false;
static int _debug_filter_version = 0;
static LogDebugFilter _debug_filters[64];
static int _debug_filter_count = 0;

IO log_stderr_output;

extern void log_init_platform(IO *output);
extern void log_exit_platform(void);
extern void log_set_output_platform(IO *output);
extern void log_apply_color_platform(LogLevel level, bool begin);
extern bool log_is_included_platform(LogLevel level, LogSource *source,
                                     LogDebugGroup debug_group);
extern void log_write_platform(struct timeval *timestamp, LogLevel level,
                               LogSource *source, LogDebugGroup debug_group,
                               const char *function, int line,
                               const char *format, va_list arguments);

static int stderr_write(IO *io, const void *buffer, int length) {
	int rc;

	(void)io;

	rc = robust_fwrite(stderr, buffer, length);

	fflush(stderr);

	return rc;
}

static int stderr_create(IO *io) {
	if (io_create(io, "stderr", NULL, NULL, stderr_write, NULL) < 0) {
		return -1;
	}

	io->write_handle = fileno(stderr);

	return 0;
}

static void log_set_debug_filter(const char *filter) {
	const char *p = filter;
	int i = 0;
	const char *tmp;

	++_debug_filter_version;
	_debug_filter_count = 0;

	while (*p != '\0') {
		if (i >= (int)sizeof(_debug_filters) / (int)sizeof(_debug_filters[0])) {
			log_warn("Too many source names in debug filter '%s'", filter);

			return;
		}

		if (*p == '+') {
			_debug_filters[i].included = true;
		} else if (*p == '-') {
			_debug_filters[i].included = false;
		} else {
			log_warn("Unexpected char '%c' in debug filter '%s' at index %d",
			         *p, filter, (int)(p - filter));

			return;
		}

		++p;
		tmp = strchr(p, ',');

		if (tmp == NULL) {
			tmp = p + strlen(p);
		}

		if (tmp - p == 0) {
			log_warn("Empty source name in debug filter '%s' at index %d",
			         filter, (int)(p - filter));

			return;
		}

		if (tmp - p >= (int)sizeof(_debug_filters[i].source_name)) {
			log_warn("Source name '%s' is too long in debug filter '%s' at index %d",
			         p, filter, (int)(p - filter));

			return;
		}

		strncpy(_debug_filters[i].source_name, p, tmp - p);
		_debug_filters[i].source_name[tmp - p] = '\0';

		if (strcasecmp(_debug_filters[i].source_name, "common") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_COMMON;
		} else if (strcasecmp(_debug_filters[i].source_name, "event") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_EVENT;
		} else if (strcasecmp(_debug_filters[i].source_name, "packet") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_PACKET;
		} else if (strcasecmp(_debug_filters[i].source_name, "object") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_OBJECT;
		} else if (strcasecmp(_debug_filters[i].source_name, "libusb") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_LIBUSB;
		} else if (strcasecmp(_debug_filters[i].source_name, "all") == 0) {
			_debug_filters[i].source_name[0] = '\0';
			_debug_filters[i].groups = LOG_DEBUG_GROUP_ALL;
		} else {
			_debug_filters[i].groups = LOG_DEBUG_GROUP_ALL;
		}

		p = tmp;

		if (*p == ',') {
			++p;

			if (*p == '\0') {
				log_warn("Debug filter '%s' ends with a trailing comma", filter);

				return;
			}
		}

		++i;
	}

	_debug_filter_count = i;
}

// NOTE: assumes that _mutex is locked
static void log_set_output_unlocked(IO *output, LogRotateFunction rotate) {
	IOStatus status;

	_output = output;
	_output_size = -1;
	_rotate = rotate;
	_rotate_countdown = ROTATE_COUNTDOWN;

	if (_output != NULL && _rotate != NULL) {
		if (io_status(_output, &status) >= 0) {
			_output_size = status.size;
		}
	}

	log_set_output_platform(_output);
}

// NOTE: assumes that _mutex is locked
static int log_write(struct timeval *timestamp, LogLevel level, LogSource *source,
                     LogDebugGroup debug_group, const char *function, int line,
                     const char *format, va_list arguments) {
	char buffer[1024] = "<unknown>";
	int length;

	if (_output == NULL) {
		return 0;
	}

	log_format(buffer, sizeof(buffer), timestamp, level, source, debug_group,
	           function, line, NULL, format, arguments);

	log_apply_color_platform(level, true);

	length = io_write(_output, buffer, strlen(buffer));

	log_apply_color_platform(level, false);

	return length;
}

void log_init(void) {
	const char *filter;

	mutex_create(&_mutex);

	_level = config_get_option_value("log.level")->symbol;

	stderr_create(&log_stderr_output);

	_output = &log_stderr_output;
	_output_size = -1;

	log_init_platform(_output);

	filter = config_get_option_value("log.debug_filter")->string;

	if (filter != NULL) {
		log_set_debug_filter(filter);
	}
}

void log_exit(void) {
	log_exit_platform();

	mutex_destroy(&_mutex);
}

void log_lock(void) {
	mutex_lock(&_mutex);
}

void log_unlock(void) {
	mutex_unlock(&_mutex);
}

void log_enable_debug_override(const char *filter) {
	_debug_override = true;

	log_set_debug_filter(filter);
}

LogLevel log_get_effective_level(void) {
	return _debug_override ? LOG_LEVEL_DEBUG : _level;
}

// if a ROTATE function is given, then the given OUTPUT has to support io_status
void log_set_output(IO *output, LogRotateFunction rotate) {
	log_lock();
	log_set_output_unlocked(output, rotate);
	log_unlock();
}

void log_get_output(IO **output, LogRotateFunction *rotate) {
	if (output != NULL) {
		*output = _output;
	}

	if (rotate != NULL) {
		*rotate = _rotate;
	}
}

bool log_is_included(LogLevel level, LogSource *source, LogDebugGroup debug_group) {
	const char *p;
	int i;

	// if the source name is not set yet use the last part of its __FILE__
	if (source->name == NULL) {
		source->name = source->file;
		p = strrchr(source->name, '/');

		if (p != NULL) {
			source->name = p + 1;
		}

		p = strrchr(source->name, '\\');

		if (p != NULL) {
			source->name = p + 1;
		}
	}

	if (!_debug_override && level > _level) {
		// primary output excluded by level, check secondary output
		return log_is_included_platform(level, source, debug_group);
	}

	if (level != LOG_LEVEL_DEBUG) {
		return true;
	}

	// check if source's debug-groups are outdated, if yes try to update them
	if (source->debug_filter_version < _debug_filter_version) {
		log_lock();

		// after gaining the mutex check that the source's debug-groups are
		// still outdated and nobody else has updated them in the meantime
		if (source->debug_filter_version < _debug_filter_version) {
			source->debug_filter_version = _debug_filter_version;
			source->included_debug_groups = LOG_DEBUG_GROUP_ALL;

			for (i = 0; i < _debug_filter_count; ++i) {
				if (_debug_filters[i].source_name[0] == '\0' ||
				    strcasecmp(source->name, _debug_filters[i].source_name) == 0) {
					if (_debug_filters[i].included) {
						source->included_debug_groups |= _debug_filters[i].groups;
					} else {
						source->included_debug_groups &= ~_debug_filters[i].groups;
					}
				}
			}
		}

		log_unlock();
	}

	// now the debug-groups are guaranteed to be up to date
	if ((source->included_debug_groups & debug_group) != 0) {
		return true;
	}

	// primary output excluded by debug-groups, check secondary output
	return log_is_included_platform(level, source, debug_group);
}

void log_message(LogLevel level, LogSource *source, LogDebugGroup debug_group,
                 bool rotate_allowed, const char *function, int line, const char *format, ...) {
	struct timeval timestamp;
	va_list arguments;
	int length;
	LogLevel rotate_level = LOG_LEVEL_DUMMY;
	char rotate_message[1024] = "<unknown>";

	if (level == LOG_LEVEL_DUMMY) {
		return; // should never be reachable
	}

	// record timestamp before locking the mutex. this results in more accurate
	// timing of log message if the mutex is contended
	if (gettimeofday(&timestamp, NULL) < 0) {
		timestamp.tv_sec = time(NULL);
		timestamp.tv_usec = 0;
	}

	// call log writers
	log_lock();

	if ((level <= _level || _debug_override) &&
	    (level != LOG_LEVEL_DEBUG ||
	     (source->included_debug_groups & debug_group) != 0)) {
		va_start(arguments, format);

		length = log_write(&timestamp, level, source, debug_group, function,
		                   line, format, arguments);

		va_end(arguments);

		if (_output_size >= 0 && length >= 0) {
			_output_size += length;
		}
	}

	if (log_is_included_platform(level, source, debug_group)) {
		va_start(arguments, format);
		log_write_platform(&timestamp, level, source, debug_group, function,
		                   line, format, arguments);
		va_end(arguments);
	}

	// rotate output
	if (_rotate_countdown > 0) {
		--_rotate_countdown;
	}

	if (_rotate != NULL && rotate_allowed &&
	    _output_size >= MAX_OUTPUT_SIZE && _rotate_countdown <= 0) {
		if (_rotate(_output, &rotate_level, rotate_message, sizeof(rotate_message)) < 0) {
			log_set_output_unlocked(NULL, NULL);
		} else {
			log_set_output_unlocked(_output, _rotate);
		}
	}

	log_unlock();

	if (rotate_level != LOG_LEVEL_DUMMY) {
		log_message_checked(rotate_level,
		                    rotate_level == LOG_LEVEL_DEBUG
		                    ? LOG_DEBUG_GROUP_COMMON
		                    : LOG_DEBUG_GROUP_NONE,
		                    false,
		                    "%s",
		                    rotate_message);
	}
}

void log_format(char *buffer, int length, struct timeval *timestamp,
                LogLevel level, LogSource *source, LogDebugGroup debug_group,
                const char *function, int line, const char *message,
                const char *format, va_list arguments) {
	time_t unix_seconds;
	struct tm localized_timestamp;
	char formatted_timestamp[64] = "<unknown>";
	char level_char;
	char *debug_group_name = "";
	char line_str[16] = "<unknown>";
	int offset;

	// copy value to time_t variable because timeval.tv_sec and time_t
	// can have different sizes between different compilers and compiler
	// version and platforms. for example with WDK 7 both are 4 byte in
	// size, but with MSVC 2010 time_t is 8 byte in size but timeval.tv_sec
	// is still 4 byte in size.
	unix_seconds = timestamp->tv_sec;

	// format time
	if (localtime_r(&unix_seconds, &localized_timestamp) != NULL) {
		strftime(formatted_timestamp, sizeof(formatted_timestamp),
		         "%Y-%m-%d %H:%M:%S", &localized_timestamp);
	}

	// format level
	switch (level) {
	case LOG_LEVEL_ERROR: level_char = 'E'; break;
	case LOG_LEVEL_WARN:  level_char = 'W'; break;
	case LOG_LEVEL_INFO:  level_char = 'I'; break;
	case LOG_LEVEL_DEBUG: level_char = 'D'; break;
	default:              level_char = 'U'; break;
	}

	// format debug group
	switch (debug_group) {
	case LOG_DEBUG_GROUP_EVENT:  debug_group_name = "event|";  break;
	case LOG_DEBUG_GROUP_PACKET: debug_group_name = "packet|"; break;
	case LOG_DEBUG_GROUP_OBJECT: debug_group_name = "object|"; break;
	case LOG_DEBUG_GROUP_LIBUSB:                               break;
	default:                                                   break;
	}

	// format line
	snprintf(line_str, sizeof(line_str), "%d", line);

	// format prefix
	snprintf(buffer, length, "%s.%06d <%c> <%s%s:%s> ",
	         formatted_timestamp, (int)timestamp->tv_usec, level_char,
	         debug_group_name, source->name, line >= 0 ? line_str : function);

	// append/format message
	if (message != NULL) {
		string_append(buffer, length, message);
	} else {
		offset = strlen(buffer); // FIXME: avoid strlen call

		vsnprintf(buffer + offset, MAX(length - offset, 0), format, arguments);
	}

	// append newline
	string_append(buffer, length, LOG_NEWLINE);
}
