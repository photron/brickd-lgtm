/*
 * daemonlib
 * Copyright (C) 2012-2014, 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * array.c: Array specific functions
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

/*
 * an Array object stores items in a continuous block of memory and provides
 * random access to it. when items are appended/removed to/from the array then
 * other items might have to be moved in memory to keep the block of memory
 * continuous. this requires that the items are relocatable in memory. if the
 * items do not have this property then the array will allocate extra memory
 * per item and store a pointer to this extra memory in its block of memory.
 *
 * for relocatable items you're not allowed to keep pointers to them while
 * performing array operations that change the array such as appending or
 * removing items. this operations may reallocate or memmove the underlying
 * continuous block of memory and hence move the items in memory.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "macros.h"

// creates an empty (count == 0) Array object and reserve memory for the number
// of items specified by RESERVE (>= 0). each item is SIZE (> 0) bytes in size.
// if the items to store can be moved in memory then set RELOCATABLE to true,
// otherwise set it to false and the items will be at a fixed location in memory
// over their entire lifetime.
//
// returns -1 on error (sets errno) or 0 on success
int array_create(Array *array, int reserve, int size, bool relocatable) {
	reserve = GROW_ALLOCATION(reserve);

	array->allocated = 0;
	array->count = 0;
	array->size = size;
	array->relocatable = relocatable;
	array->bytes = calloc(reserve, relocatable ? size : (int)sizeof(void *));

	if (array->bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	array->allocated = reserve;

	return 0;
}

// destroys an Array object and frees the underlying memory. if an item destroy
// function DESTROY is given then it is called for each item in the array (with
// a pointer to the item as the only parameter) before the memory is freed.
void array_destroy(Array *array, ItemDestroyFunction destroy) {
	int i;
	void *item;

	if (destroy != NULL) {
		for (i = 0; i < array->count; ++i) {
			item = array_get(array, i);

			destroy(item);

			if (!array->relocatable) {
				free(item);
			}
		}
	} else if (!array->relocatable) {
		for (i = 0; i < array->count; ++i) {
			free(array_get(array, i));
		}
	}

	free(array->bytes);
}

// ensures that an Array object's underlying memory block can store at least
// the number of items specified by RESERVE (>= 0). this is useful if a larger
// number of items should be appended to the array, because if enough memory
// was reserved before appending then the append operations don't have to grow
// the array anymore, but can just use the memory that was allocated before.
//
// returns -1 on error (sets errno) or 0 on success
int array_reserve(Array *array, int reserve) {
	int size = array->relocatable ? array->size : (int)sizeof(void *);
	uint8_t *bytes;

	if (array->allocated >= reserve) {
		return 0;
	}

	reserve = GROW_ALLOCATION(reserve);
	bytes = realloc(array->bytes, reserve * size);

	if (bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	memset(bytes + array->allocated * size, 0, (reserve - array->allocated) * size);

	array->allocated = reserve;
	array->bytes = bytes;

	return 0;
}

// resizes an Array object to the number of items given by COUNT (>= 0). if
// the array grows then new items are appended to the array and their memory is
// initialized to zero. if the array shrinks then the excess items at the end
// of the array are removed. if an item destroy function DESTROY is given then
// it is called for each item that is removed (with a pointer to the item as
// the only parameter).
//
// returns -1 on error (sets errno) or 0 on success
int array_resize(Array *array, int count, ItemDestroyFunction destroy) {
	int rc;
	int i;
	void *item;

	if (array->count < count) { // grow
		rc = array_reserve(array, count);

		if (rc < 0) {
			return rc;
		}

		if (!array->relocatable) {
			for (i = array->count; i < count; ++i) {
				item = calloc(1, array->size);

				if (item == NULL) {
					for (--i; i >= array->count; --i) {
						free(array_get(array, i));
					}

					errno = ENOMEM;

					return -1;
				}

				*(void **)(array->bytes + sizeof(void *) * i) = item;
			}
		}
	} else if (array->count > count) { // shrink
		if (destroy != NULL) {
			for (i = count; i < array->count; ++i) {
				item = array_get(array, i);

				destroy(item);

				if (!array->relocatable) {
					free(item);
				}
			}
		} else if (!array->relocatable) {
			for (i = count; i < array->count; ++i) {
				free(array_get(array, i));
			}
		}
	}

	array->count = count;

	return 0;
}

// appends a new item to the end of an Array object. the memory of this item
// is initialized to zero.
//
// returns NULL on error (sets errno) or a pointer to the new item on success
void *array_append(Array *array) {
	void *item;

	if (array_reserve(array, array->count + 1) < 0) {
		return NULL;
	}

	if (array->relocatable) {
		item = array->bytes + array->size * array->count;
	} else {
		item = calloc(1, array->size);

		if (item == NULL) {
			errno = ENOMEM;

			return NULL;
		}

		*(void **)(array->bytes + sizeof(void *) * array->count) = item;
	}

	++array->count;

	return item;
}

// removes the item at the given INDEX (>= 0 and < count) from an Array object.
// if an item destroy function DESTROY is given then it is called (with a
// pointer to the item as the only parameter) before it is removed.
void array_remove(Array *array, int index, ItemDestroyFunction destroy) {
	void *item = array_get(array, index);
	int size = array->relocatable ? array->size : (int)sizeof(void *);
	int tail;

	if (destroy != NULL) {
		destroy(item);
	}

	if (!array->relocatable) {
		free(item);
	}

	tail = (array->count - index - 1) * size;

	if (tail > 0) {
		memmove(array->bytes + size * index, array->bytes + size * (index + 1), tail);
	}

	memset(array->bytes + size * (array->count - 1), 0, size);

	--array->count;
}

// returns a pointer to the item at the given INDEX (>= 0 and < count)
void *array_get(Array *array, int index) {
	if (array->relocatable) {
		return array->bytes + array->size * index;
	} else {
		return *(void **)(array->bytes + sizeof(void *) * index);
	}
}

// swaps the content of an Array object with the content of another an Array object
void array_swap(Array *array, Array *other) {
	int allocated = other->allocated;
	int count = other->count;
	int size = other->size;
	bool relocatable = other->relocatable;
	uint8_t *bytes = other->bytes;

	other->allocated = array->allocated;
	other->count = array->count;
	other->size = array->size;
	other->relocatable = array->relocatable;
	other->bytes = array->bytes;

	array->allocated = allocated;
	array->count = count;
	array->size = size;
	array->relocatable = relocatable;
	array->bytes = bytes;
}
