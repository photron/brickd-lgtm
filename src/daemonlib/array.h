/*
 * daemonlib
 * Copyright (C) 2012-2014, 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * array.h: Array specific functions
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

#ifndef DAEMONLIB_ARRAY_H
#define DAEMONLIB_ARRAY_H

#include <stdbool.h>
#include <stdint.h>

#include "utils.h"

typedef struct {
	int allocated; // number of allocated items
	int count; // number of stored items
	int size; // size of a single item in bytes
	bool relocatable; // true if item can be moved in memory
	uint8_t *bytes;
} Array;

int array_create(Array *array, int reserve, int size, bool relocatable);
void array_destroy(Array *array, ItemDestroyFunction destroy);

int array_reserve(Array *array, int count);
int array_resize(Array *array, int count, ItemDestroyFunction destroy);

void *array_append(Array *array);
void array_remove(Array *array, int i, ItemDestroyFunction destroy);

void *array_get(Array *array, int i);

void array_swap(Array *array, Array *other);

#endif // DAEMONLIB_ARRAY_H
