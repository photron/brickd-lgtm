/*
 * daemonlib
 * Copyright (C) 2013-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * queue.h: Queue specific functions
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

#ifndef DAEMONLIB_QUEUE_H
#define DAEMONLIB_QUEUE_H

#include <stdint.h>

#include "utils.h"

typedef struct _QueueNode QueueNode;

struct _QueueNode {
	QueueNode *next;
};

typedef struct {
	int count; // number of items in the queue
	int size; // size of a single item in bytes
	QueueNode *head;
	QueueNode *tail;
} Queue;

int queue_create(Queue *queue, int size);
void queue_destroy(Queue *queue, ItemDestroyFunction destroy);

void *queue_push(Queue *queue);
void queue_pop(Queue *queue, ItemDestroyFunction destroy);
void *queue_peek(Queue *queue);

#endif // DAEMONLIB_QUEUE_H
