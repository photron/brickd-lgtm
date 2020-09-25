/*
 * brickd
 * Copyright (C) 2012-2014, 2016 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * stack.h: Stack specific functions
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

#ifndef BRICKD_STACK_H
#define BRICKD_STACK_H

#include <stdbool.h>

#include <daemonlib/array.h>
#include <daemonlib/packet.h>

typedef struct _Stack Stack;

typedef struct {
	uint32_t uid; // always little endian
	uint64_t opaque;
} Recipient;

typedef int (*StackDispatchRequestFunction)(Stack *stack, Packet *request, Recipient *recipient);

#define STACK_MAX_NAME_LENGTH 128

struct _Stack {
	char name[STACK_MAX_NAME_LENGTH]; // for display purpose
	StackDispatchRequestFunction dispatch_request;
	Array recipients;
};

int stack_create(Stack *stack, const char *name,
                 StackDispatchRequestFunction dispatch_request);
void stack_destroy(Stack *stack);

int stack_add_recipient(Stack *stack, uint32_t uid /* always little endian */, uint64_t opaque);
Recipient *stack_get_recipient(Stack *stack, uint32_t uid /* always little endian */);

int stack_dispatch_request(Stack *stack, Packet *request, bool force);

void stack_announce_disconnect(Stack *stack);

#endif // BRICKD_STACK_H
