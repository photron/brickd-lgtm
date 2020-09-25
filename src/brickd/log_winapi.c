/*
 * brickd
 * Copyright (C) 2012-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_winapi.c: Windows log file, live log view and debugger output handling
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

#include <io.h>
#include <stdbool.h>
#ifndef _MSC_VER
	#include <sys/time.h>
#endif
#include <windows.h>

#include <daemonlib/log.h>
#include <daemonlib/threads.h>
#include <daemonlib/utils.h>

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

enum { // bitmask
	LOG_PIPE_MESSAGE_FLAG_LIBUSB = 0x0001
};

#include <daemonlib/packed_begin.h>

typedef struct {
	uint16_t length;
	uint8_t flags;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	char source[128];
	int line;
	char message[1024];
} ATTRIBUTE_PACKED LogPipeMessage;

#include <daemonlib/packed_end.h>

typedef struct {
	const char *name;
	HANDLE handle;
	bool connected;
	Thread thread;
	bool running;
	Semaphore handshake;
} LogPipe;

#define NAMED_PIPE_BUFFER_LENGTH (sizeof(LogPipeMessage) * 4)
#define FOREGROUND_ALL (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define BACKGROUND_ALL (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

static bool _debugger_present = false;
static IO *_output = NULL;
static HANDLE _console = NULL;
static WORD _default_attributes = 0;
static bool _pipes_initialized = false;
static LogPipe _pipes[4];

static const char *_pipes_names[4][2] = {
	{"error", "\\\\.\\pipe\\tinkerforge-brick-daemon-error-log"},
	{"warn", "\\\\.\\pipe\\tinkerforge-brick-daemon-warn-log"},
	{"info", "\\\\.\\pipe\\tinkerforge-brick-daemon-info-log"},
	{"debug", "\\\\.\\pipe\\tinkerforge-brick-daemon-debug-log"}
};

static HANDLE _pipes_write_event = NULL;
static HANDLE _pipes_stop_event = NULL;

void log_set_output_platform(IO *output);

static WORD log_prepare_color_attributes(WORD color) {
	WORD attributes = _default_attributes;
	WORD background = BACKGROUND_INTENSITY;

	if ((color & FOREGROUND_RED) != 0) {
		background |= BACKGROUND_RED;
	}

	if ((color & FOREGROUND_GREEN) != 0) {
		background |= BACKGROUND_GREEN;
	}

	if ((color & FOREGROUND_BLUE) != 0) {
		background |= BACKGROUND_BLUE;
	}

	if ((attributes & BACKGROUND_ALL) == background) {
		attributes &= ~FOREGROUND_ALL;
		attributes |= FOREGROUND_ALL & ~color;
	} else {
		attributes &= ~FOREGROUND_ALL;
		attributes |= color;
	}

	return attributes;
}

static void log_connect_pipe(void *opaque) {
	LogPipe *pipe = opaque;
	HANDLE overlapped_event;
	HANDLE events[2];
	int rc;
	OVERLAPPED overlapped;
	uint8_t byte;

	// create connect/read event
	overlapped_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (overlapped_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create overlapped connect/read event for %s log pipe: %s (%d)",
		          pipe->name, get_errno_name(rc), rc);

		// need to release the handshake in all cases, otherwise
		// log_init_platform will block forever in semaphore_acquire
		semaphore_release(&pipe->handshake);

		return;
	}

	// start loop
	pipe->running = true;
	semaphore_release(&pipe->handshake);

	log_debug("Started pipe connect thread for %s log pipe", pipe->name);

	while (pipe->running) {
		// connect pipe
		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = overlapped_event;

		if (ConnectNamedPipe(pipe->handle, &overlapped)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not connect named pipe for %s log pipe: %s (%d)",
			          pipe->name, get_errno_name(rc), rc);

			goto cleanup;
		}

		rc = GetLastError();

		// wait for connect/stop event
		switch (rc)  {
		case ERROR_IO_PENDING: // connection in progress
			events[0] = _pipes_stop_event;
			events[1] = overlapped_event;

			rc = WaitForMultipleObjects(2, events, FALSE, INFINITE);

			if (rc == WAIT_OBJECT_0) {
				// pipe connect thread stopped
				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				pipe->connected = true;

				log_info("Log Viewer connected to %s log pipe", pipe->name);
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				log_error("Could not wait for connect/stop event of %s log pipe: %s (%d)",
				          pipe->name, get_errno_name(rc), rc);

				goto cleanup;
			}

			break;

		case ERROR_PIPE_CONNECTED: // already connected
			break;

		case ERROR_NO_DATA: // last connection was not properly closed, retry
			continue;

		default:
			rc += ERRNO_WINAPI_OFFSET;

			log_error("Could not connect %s log pipe: %s (%d)",
			          pipe->name, get_errno_name(rc), rc);

			goto cleanup;
		}

		// read from pipe to detect client disconnect
		for (;;) {
			ResetEvent(overlapped_event);

			memset(&overlapped, 0, sizeof(overlapped));
			overlapped.hEvent = overlapped_event;

			if (ReadFile(pipe->handle, &byte, 1, NULL, &overlapped)) {
				continue;
			}

			if (GetLastError() != ERROR_IO_PENDING) {
				DisconnectNamedPipe(pipe->handle);

				log_info("Log Viewer disconnected from %s log pipe", pipe->name);

				pipe->connected = false;

				break;
			}

			events[0] = _pipes_stop_event;
			events[1] = overlapped_event;

			rc = WaitForMultipleObjects(2, events, FALSE, INFINITE);

			if (rc == WAIT_OBJECT_0) {
				// pipe connect thread stopped
				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				DisconnectNamedPipe(pipe->handle);

				log_info("Log Viewer disconnected from %s log pipe", pipe->name);

				pipe->connected = false;

				break;
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				log_error("Could not wait for connect/stop event of %s log pipe: %s (%d)",
				          pipe->name, get_errno_name(rc), rc);

				goto cleanup;
			}
		}
	}

cleanup:
	CloseHandle(overlapped_event);

	pipe->running = false;
}

void log_init_platform(IO *output) {
	int rc;
	int i;

	_debugger_present = IsDebuggerPresent();

	log_set_output_platform(output);

	// initialize pipes
	memset(_pipes, 0, sizeof(_pipes));

	for (i = 0; i < 4; ++i) {
		_pipes[i].name = _pipes_names[i][0];
		_pipes[i].handle = INVALID_HANDLE_VALUE;
	}

	// create stop event
	_pipes_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);

	if (_pipes_stop_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create pipe stop event: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}

	// create write event
	_pipes_write_event = CreateEventA(NULL, TRUE, FALSE, NULL);

	if (_pipes_write_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create pipe overlapped write event: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}

	// create pipes
	for (i = 0; i < 4; ++i) {
		_pipes[i].handle = CreateNamedPipeA(_pipes_names[i][1],
		                                    PIPE_ACCESS_DUPLEX |
		                                    FILE_FLAG_OVERLAPPED |
		                                    FILE_FLAG_FIRST_PIPE_INSTANCE,
		                                    PIPE_TYPE_MESSAGE |
		                                    PIPE_WAIT,
		                                    1,
		                                    NAMED_PIPE_BUFFER_LENGTH,
		                                    NAMED_PIPE_BUFFER_LENGTH,
		                                    0,
		                                    NULL);

		if (_pipes[i].handle == INVALID_HANDLE_VALUE) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			// ERROR_PIPE_BUSY/ERROR_ACCESS_DENIED means pipe already exists
			// because another instance of brickd is already running. no point
			// in logging this cases on debug level here, as log level is still
			// at the default info level yet. so just ignore these two cases
			if (rc != ERRNO_WINAPI_OFFSET + ERROR_PIPE_BUSY &&
			    rc != ERRNO_WINAPI_OFFSET + ERROR_ACCESS_DENIED) {
				log_error("Could not create %s pipe: %s (%d)",
				          _pipes[i].name, get_errno_name(rc), rc);
			}
		} else {
			// create named pipe connect thread
			if (semaphore_create(&_pipes[i].handshake) < 0) {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				log_error("Could not create handshake semaphore for %s pipe: %s (%d)",
				          _pipes[i].name, get_errno_name(rc), rc);
			} else {
				thread_create(&_pipes[i].thread, log_connect_pipe, &_pipes[i]);

				semaphore_acquire(&_pipes[i].handshake);
				semaphore_destroy(&_pipes[i].handshake);
			}
		}
	}

	_pipes_initialized = true;
}

void log_exit_platform(void) {
	int i;

	if (!_pipes_initialized) {
		return;
	}

	SetEvent(_pipes_stop_event);

	for (i = 0; i < 4; ++i) {
		if (_pipes[i].running) {
			thread_join(&_pipes[i].thread);
			thread_destroy(&_pipes[i].thread);
		}

		_pipes[i].connected = false;

		if (_pipes[i].handle != INVALID_HANDLE_VALUE) {
			CloseHandle(_pipes[i].handle);
		}
	}

	CloseHandle(_pipes_stop_event);
	CloseHandle(_pipes_write_event);
}

void log_set_output_platform(IO *output) {
	HANDLE console;
	CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;

	_output = output;
	_console = NULL;
	_default_attributes = 0;

	if (_output != &log_stderr_output) {
		return;
	}

	console = (HANDLE)_get_osfhandle(_output->write_handle);

	if (console == INVALID_HANDLE_VALUE) {
		return;
	}

	if (GetFileType(console) != FILE_TYPE_CHAR) {
		return;
	}

	if (!GetConsoleScreenBufferInfo(console, &screen_buffer_info)) {
		return;
	}

	_console = console;
	_default_attributes = screen_buffer_info.wAttributes;
}

void log_apply_color_platform(LogLevel level, bool begin) {
	WORD attributes = _default_attributes;

	if (_console == NULL) {
		return;
	}

	if (begin) {
		switch (level) {
		case LOG_LEVEL_DUMMY: // ignore this to avoid compiler warning
			break;

		case LOG_LEVEL_ERROR:
			attributes = log_prepare_color_attributes(FOREGROUND_RED | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_WARN:
			// FIXME: select blue or yellow depending on background color
			attributes = log_prepare_color_attributes(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_INFO:
			attributes = log_prepare_color_attributes(FOREGROUND_WHITE | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_DEBUG:
			attributes = log_prepare_color_attributes(FOREGROUND_WHITE);
			break;
		}

		SetConsoleTextAttribute(_console, attributes);
	} else {
		SetConsoleTextAttribute(_console, _default_attributes);
	}
}

bool log_is_included_platform(LogLevel level, LogSource *source,
                              LogDebugGroup debug_group) {
	(void)source;
	(void)debug_group;

	if (_debugger_present) {
		return true;
	}

	switch (level) {
	case LOG_LEVEL_DUMMY: // ignore this to avoid compiler warning
		return false;

	case LOG_LEVEL_ERROR:
		return _pipes[3].connected || _pipes[2].connected || _pipes[1].connected || _pipes[0].connected;

	case LOG_LEVEL_WARN:
		return _pipes[3].connected || _pipes[2].connected || _pipes[1].connected;

	case LOG_LEVEL_INFO:
		return _pipes[3].connected || _pipes[2].connected;

	case LOG_LEVEL_DEBUG:
		return _pipes[3].connected;
	}

	return false;
}

// NOTE: assumes that _mutex (in log.c) is locked
void log_write_platform(struct timeval *timestamp, LogLevel level,
                        LogSource *source, LogDebugGroup debug_group,
                        const char *function, int line,
                        const char *format, va_list arguments) {
	int i;
	LogPipe *pipe = NULL;
	LogPipeMessage message;
	char buffer[1024] = "<unknown>\n";
	OVERLAPPED overlapped;
	DWORD bytes_written;

	(void)debug_group;

	for (i = level; i < 4; ++i) {
		if (_pipes[i].connected) {
			pipe = &_pipes[i];
			break;
		}
	}

	if (pipe == NULL && !_debugger_present) {
		return;
	}

	vsnprintf(message.message, sizeof(message.message), format, arguments);

	message.length = sizeof(message);
	message.flags = source->libusb ? LOG_PIPE_MESSAGE_FLAG_LIBUSB : 0;
	message.timestamp = (uint64_t)timestamp->tv_sec * 1000000 + timestamp->tv_usec;
	message.level = level;
	message.line = line;

	string_copy(message.source, sizeof(message.source),
	            source->libusb ? function : source->name, -1);

	if (_debugger_present) {
		log_format(buffer, sizeof(buffer), timestamp, level, source, debug_group,
		           function, line, message.message, "", arguments);

		OutputDebugStringA(buffer);
	}

	if (pipe != NULL) {
		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = _pipes_write_event;

		if (!WriteFile(pipe->handle, &message, sizeof(message), NULL, &overlapped) &&
		    GetLastError() == ERROR_IO_PENDING) {
			// wait for result of overlapped I/O to avoid a race condition with
			// the next WriteFile call that will reuse the same event handle
			GetOverlappedResult(pipe->handle, &overlapped, &bytes_written, TRUE);
		}
	}
}
