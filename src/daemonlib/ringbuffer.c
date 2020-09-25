/*
 * daemonlib
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * ringbuffer.c: Simple uint8 ringbuffer implementation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdio.h>

#include "ringbuffer.h"

#include "macros.h"

uint16_t ringbuffer_get_used(Ringbuffer *rb) {
	if(rb->end < rb->start) {
		return rb->size + rb->end - rb->start;
	}

	return rb->end - rb->start;
}

uint16_t ringbuffer_get_free(Ringbuffer *rb) {
	const uint16_t free = rb->size - ringbuffer_get_used(rb);

	if(free < rb->low_watermark) {
		rb->low_watermark = free;
	}

	return free;
}

bool ringbuffer_is_empty(Ringbuffer *rb) {
	return rb->start == rb->end;
}

bool ringbuffer_is_full(Ringbuffer *rb) {
	return ringbuffer_get_free(rb) < 2;
}

bool ringbuffer_add(Ringbuffer *rb, const uint8_t data) {
	rb->buffer[rb->end] = data;
	rb->end++;
	if(rb->end >= rb->size) {
		rb->end = 0;
	}

	if(rb->end == rb->start) {
		rb->overflows++;
		if(rb->end == 0) {
			rb->end = rb->size-1;
		} else {
			rb->end--;
		}
		return false;
	}

	return true;
}

void ringbuffer_remove(Ringbuffer *rb, const uint16_t num) {
	// Make sure that we don't remove more then is available in the buffer
	uint16_t incr = MIN(ringbuffer_get_used(rb), num);

	rb->start += incr;
	if(rb->start >= rb->size) {
		rb->start -= rb->size;
	}
}

bool ringbuffer_get(Ringbuffer *rb, uint8_t *data) {
	if(ringbuffer_is_empty(rb)) {
		return false;
	}

	*data = rb->buffer[rb->start];
	rb->start++;
	if(rb->start >= rb->size) {
		rb->start = 0;
	}

	return true;
}

void ringbuffer_init(Ringbuffer *rb, const uint16_t size, uint8_t *buffer) {
	rb->overflows     = 0;
	rb->start         = 0;
	rb->end           = 0;
	rb->size          = size;
	rb->low_watermark = size;
	rb->buffer        = buffer;
}

void ringbuffer_print(Ringbuffer *rb) {
	int32_t end = rb->end - rb->start;
	uint16_t i;

	if(end < 0) {
		end += rb->size;
	}

	printf("Ringbuffer (start %u, end %u, size %u, low %u, overflows %u): [\n",
	       rb->start, rb->end, rb->size, rb->low_watermark, rb->overflows);

	for(i = 0; i < end; i++) {
		if((i % 16) == 0) {
			printf("    ");
		}

		printf("%x, ", rb->buffer[(rb->start + i) % rb->size]);

		if((i % 16) == 15) {
			printf("\n");
		}
	}

	printf("]\n");
}
