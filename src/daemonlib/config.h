/*
 * daemonlib
 * Copyright (C) 2012, 2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * config.h: Config file subsystem
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

#ifndef DAEMONLIB_CONFIG_H
#define DAEMONLIB_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "log.h"

typedef enum {
	CONFIG_OPTION_TYPE_STRING = 0,
	CONFIG_OPTION_TYPE_INTEGER,
	CONFIG_OPTION_TYPE_BOOLEAN,
	CONFIG_OPTION_TYPE_SYMBOL
} ConfigOptionType;

typedef int (*ConfigOptionSymbolParseValueFunction)(const char *string, int *value);
typedef const char *(*ConfigOptionSymbolFormatNameFunction)(int value);

typedef struct {
	char *string;
	int integer;
	bool boolean;
	int symbol;
} ConfigOptionValue;

typedef struct {
	const char *name;
	ConfigOptionType type;
	int string_min_length;
	int string_max_length;
	int integer_min;
	int integer_max;
	ConfigOptionSymbolParseValueFunction symbol_parse_value;
	ConfigOptionSymbolFormatNameFunction symbol_format_name;
	ConfigOptionValue default_value;
	ConfigOptionValue value;
} ConfigOption;

#define CONFIG_OPTION_STRING_LENGTH_RANGE(min, max) \
	min, max

#define CONFIG_OPTION_INTEGER_RANGE(min, max) \
	min, max

#define CONFIG_OPTION_SYMBOL_FUNCTIONS(parse_value, format_name) \
	parse_value, format_name

#define CONFIG_OPTION_VALUE_STRING_INITIALIZER(value) \
	{ value, 0, false, -1 }

#define CONFIG_OPTION_VALUE_INTEGER_INITIALIZER(value) \
	{ NULL, value, false, -1 }

#define CONFIG_OPTION_VALUE_BOOLEAN_INITIALIZER(value) \
	{ NULL, 0, value, -1 }

#define CONFIG_OPTION_VALUE_SYMBOL_INITIALIZER(value) \
	{ NULL, 0, false, value }

#define CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	{ NULL, 0, false, -1 }

#define CONFIG_OPTION_STRING_INITIALIZER(name, min, max, default_value) \
	{ \
		name, \
		CONFIG_OPTION_TYPE_STRING, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(min, max), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_SYMBOL_FUNCTIONS(NULL, NULL), \
		CONFIG_OPTION_VALUE_STRING_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_INTEGER_INITIALIZER(name, min, max, default_value) \
	{ \
		name, \
		CONFIG_OPTION_TYPE_INTEGER, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(min, max), \
		CONFIG_OPTION_SYMBOL_FUNCTIONS(NULL, NULL), \
		CONFIG_OPTION_VALUE_INTEGER_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_BOOLEAN_INITIALIZER(name, default_value) \
	{ \
		name, \
		CONFIG_OPTION_TYPE_BOOLEAN, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_SYMBOL_FUNCTIONS(NULL, NULL), \
		CONFIG_OPTION_VALUE_BOOLEAN_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_SYMBOL_INITIALIZER(name, parse_value, format_name, default_value) \
	{ \
		name, \
		CONFIG_OPTION_TYPE_SYMBOL, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_SYMBOL_FUNCTIONS(parse_value, format_name), \
		CONFIG_OPTION_VALUE_SYMBOL_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_NULL_INITIALIZER \
	{ \
		NULL, \
		CONFIG_OPTION_TYPE_STRING, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_SYMBOL_FUNCTIONS(NULL, NULL), \
		CONFIG_OPTION_VALUE_STRING_INITIALIZER(NULL), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

int config_parse_log_level(const char *string, int *value);
const char *config_format_log_level(int level);

int config_check(const char *filename);

void config_init(const char *filename);
void config_exit(void);

bool config_has_error(void);
bool config_has_warning(void);

ConfigOptionValue *config_get_option_value(const char *name);

#endif // DAEMONLIB_CONFIG_H
