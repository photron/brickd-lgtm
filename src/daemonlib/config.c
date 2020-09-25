/*
 * daemonlib
 * Copyright (C) 2012-2018, 2020 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * config.c: Config file subsystem
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "conf_file.h"
#include "enum.h"
#include "utils.h"

static bool _check_only = false;
static bool _has_error = false;
static bool _has_warning = false;
static bool _using_default_values = true;
static ConfigOption _invalid = CONFIG_OPTION_STRING_INITIALIZER("<invalid>", 0, -1, "<invalid>");

static EnumValueName _log_level_enum_value_names[] = {
	{ LOG_LEVEL_ERROR, "error" },
	{ LOG_LEVEL_WARN,  "warn" },
	{ LOG_LEVEL_INFO,  "info" },
	{ LOG_LEVEL_DEBUG, "debug" },
	{ -1,              NULL }
};

extern ConfigOption config_options[];

#define config_error(...) config_message(&_has_error, __VA_ARGS__)
#define config_warn(...) config_message(&_has_warning, __VA_ARGS__)

static void config_message(bool *has_message, const char *format, ...) ATTRIBUTE_FMT_PRINTF(2, 3);

static void config_message(bool *has_message, const char *format, ...) {
	va_list arguments;
#ifdef DAEMONLIB_UWP_BUILD
	char buffer[1024];
#endif

	*has_message = true;

	if (!_check_only) {
		return;
	}

	va_start(arguments, format);

#ifdef DAEMONLIB_UWP_BUILD
	vsnprintf(buffer, sizeof(buffer), format, arguments);

	OutputDebugStringA(buffer);
	OutputDebugStringA("\n");
#else
	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	fflush(stderr);
#endif

	va_end(arguments);
}

static void config_reset(void) {
	int i = 0;

	_using_default_values = true;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (config_options[i].type == CONFIG_OPTION_TYPE_STRING) {
			if (config_options[i].value.string != config_options[i].default_value.string) {
				free(config_options[i].value.string);
			}
		}

		memcpy(&config_options[i].value, &config_options[i].default_value,
		       sizeof(config_options[i].value));
	}
}

static void config_report_read_warning(ConfFileReadWarning warning, int number,
                                       const char *buffer, void *opaque) {
	(void)opaque;

	switch (warning) {
	case CONF_FILE_READ_WARNING_LINE_TOO_LONG:
		config_warn("Line %d is too long: %s...", number, buffer);

		break;

	case CONF_FILE_READ_WARNING_NAME_MISSING:
		config_warn("Line %d contains no option name: %s", number, buffer);

		break;

	case CONF_FILE_READ_WARNING_EQUAL_SIGN_MISSING:
		config_warn("Line %d contains no '=' sign: %s", number, buffer);

		break;

	case CONF_FILE_READ_WARNING_NON_PRINTABLE_ASCII_CHARACTER:
		config_warn("Line %d contains non-printable ASCII character: 0x%02X", number, *buffer);

		break;

	case CONF_FILE_READ_WARNING_INCOMPLETE_ESCAPE_SEQUENCE:
		config_warn("Line %d contains incomplete escape sequence: %s", number, buffer);

		break;

	case CONF_FILE_READ_WARNING_INVALID_ESCAPE_SEQUENCE:
		config_warn("Line %d contains invalid escape sequence: %s", number, buffer);

		break;

	default:
		config_warn("Unknown warning %d in line %d", warning, number);

		break;
	}
}

int config_parse_log_level(const char *string, int *value) {
	return enum_get_value(_log_level_enum_value_names, string, value, true);
}

const char *config_format_log_level(int level) {
	return enum_get_name(_log_level_enum_value_names, level, "<unknown>");
}

int config_check(const char *filename) {
	int i;
	int length;
	int maximum_length = 0;
	int k;

	_check_only = true;

	config_init(filename);

	if (_has_error) {
		fprintf(stderr, "Error(s) occurred while reading config file '%s'\n", filename);

		config_exit();

		return -1;
	} else if (_has_warning) {
		printf("Warning(s) in config file '%s'\n", filename);
	} else if (_using_default_values) {
		printf("Config file '%s' not found, using default values\n", filename);
	} else {
		printf("No warnings or errors in config file '%s'\n", filename);
	}

	printf("\n");
	printf("Using the following config values:\n");

	for (i = 0; config_options[i].name != NULL; ++i) {
		length = strlen(config_options[i].name);

		if (length > maximum_length) {
			maximum_length = length;
		}
	}

	for (i = 0; config_options[i].name != NULL; ++i) {
		printf("  %s ", config_options[i].name);

		for (k = strlen(config_options[i].name); k < maximum_length; ++k) {
			fputs(" ", stdout);
		}

		fputs("= ", stdout);

		switch (config_options[i].type) {
		case CONFIG_OPTION_TYPE_STRING:
			if (config_options[i].value.string != NULL) {
				printf("%s", config_options[i].value.string);
			}

			break;

		case CONFIG_OPTION_TYPE_INTEGER:
			printf("%d", config_options[i].value.integer);

			break;

		case CONFIG_OPTION_TYPE_BOOLEAN:
			printf("%s", config_options[i].value.boolean ? "on" : "off");

			break;

		case CONFIG_OPTION_TYPE_SYMBOL:
			printf("%s", config_options[i].symbol_format_name(config_options[i].value.symbol));

			break;

		default:
			printf("<unknown-type>");

			break;
		}

		printf("\n");
	}

	config_exit();

	return _has_warning ? -1 : 0;
}

void config_init(const char *filename) {
	ConfFile conf_file;
	int i;
	const char *value;
	int length;
	int integer;

	config_reset();

	if (filename == NULL) {
		return;
	}

	// read config file
	if (conf_file_create(&conf_file) < 0) {
		config_error("Internal error occurred");

		return;
	}

	if (conf_file_read(&conf_file, filename, config_report_read_warning, NULL) < 0) {
		if (errno == ENOENT) {
			// ignore
		} else if (errno == ENOMEM) {
			config_error("Could not allocate memory");
		} else if (errno == EACCES) {
			config_error("Access to config file was denied");
		} else {
			config_error("Unexpected error %s (%d) occurred", get_errno_name(errno), errno);
		}

		goto cleanup;
	}

	_using_default_values = false;

	for (i = 0; config_options[i].name != NULL; ++i) {
		value = conf_file_get_option_value(&conf_file, config_options[i].name);

		if (value == NULL) {
			continue;
		}

		switch (config_options[i].type) {
		case CONFIG_OPTION_TYPE_STRING:
			if (config_options[i].value.string != config_options[i].default_value.string) {
				free(config_options[i].value.string);
				config_options[i].value.string = NULL;
			}

			length = strlen(value);

			if (length < config_options[i].string_min_length) {
				config_warn("Value '%s' for %s option is too short (minimum: %d chars)",
				            value, config_options[i].name, config_options[i].string_min_length);
			} else if (config_options[i].string_max_length >= 0 &&
			           length > config_options[i].string_max_length) {
				config_warn("Value '%s' for %s option is too long (maximum: %d chars)",
				            value, config_options[i].name, config_options[i].string_max_length);
			} else if (length > 0) {
				config_options[i].value.string = strdup(value);

				if (config_options[i].value.string == NULL) {
					config_options[i].value.string = config_options[i].default_value.string;

					config_error("Could not duplicate %s value '%s'",
					             config_options[i].name, value);

					goto cleanup;
				}
			}

			break;

		case CONFIG_OPTION_TYPE_INTEGER:
			if (parse_int(value, NULL, 10, &integer) < 0) {
				config_warn("Value '%s' for %s option is not an integer",
				            value, config_options[i].name);
			} else if (integer < config_options[i].integer_min || integer > config_options[i].integer_max) {
				config_warn("Value %d for %s option is out-of-range (minimum: %d, maximum: %d)",
				            integer, config_options[i].name,
				            config_options[i].integer_min, config_options[i].integer_max);
			} else {
				config_options[i].value.integer = integer;
			}

			break;

		case CONFIG_OPTION_TYPE_BOOLEAN:
			if (strcasecmp(value, "on") == 0) {
				config_options[i].value.boolean = true;
			} else if (strcasecmp(value, "off") == 0) {
				config_options[i].value.boolean = false;
			} else {
				config_warn("Value '%s' for %s option is invalid",
				            value, config_options[i].name);
			}

			break;

		case CONFIG_OPTION_TYPE_SYMBOL:
			if (config_options[i].symbol_parse_value(value, &config_options[i].value.symbol) < 0) {
				config_warn("Value '%s' for %s option is invalid", value, config_options[i].name);
			}

			break;
		}
	}

cleanup:
	conf_file_destroy(&conf_file);
}

void config_exit(void) {
	int i = 0;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (config_options[i].type == CONFIG_OPTION_TYPE_STRING) {
			if (config_options[i].value.string != config_options[i].default_value.string) {
				free(config_options[i].value.string);
				config_options[i].value.string = NULL;
			}
		}
	}
}

bool config_has_error(void) {
	return _has_error;
}

bool config_has_warning(void) {
	return _has_warning;
}

ConfigOptionValue *config_get_option_value(const char *name) {
	int i = 0;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (strcmp(config_options[i].name, name) == 0) {
			return &config_options[i].value;
		}
	}

	return &_invalid.value;
}
