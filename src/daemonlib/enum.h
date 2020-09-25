/*
 * daemonlib
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * enum.h: Enum value/name lookup
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

#ifndef DAEMONLIB_ENUM_H
#define DAEMONLIB_ENUM_H

#include <stdbool.h>

typedef struct {
	int value;
	const char *name;
} EnumValueName;

const char *enum_get_name(EnumValueName *value_names, int value,
                          const char *unknown);
int enum_get_value(EnumValueName *value_names, const char *name, int *value,
                   bool ignore_case);

#endif // DAEMONLIB_ENUM_H
