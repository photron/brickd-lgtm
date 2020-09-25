/*
 * daemonlib
 * Copyright (C) 2017, 2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * utils_uwp.h: Utility functions for Universal Windows Platform
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

#ifndef DAEMONLIB_UTILS_UWP_H
#define DAEMONLIB_UTILS_UWP_H

char *string_convert_ascii(Platform::String ^string);

Platform::String ^ascii_convert_string(const char *ascii);

#endif // DAEMONLIB_UTILS_UWP_H
