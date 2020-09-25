/*
 * daemonlib
 * Copyright (C) 2013-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * queue.c: Queue specific functions
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
 * a Queue object stores items in a single linked list and allows to add items
 * to its tail and to remove items from its head. in contrast to an Array object
 * there is no need for special handling of non-relocatable items because an
 * item is never moved in memory during Queue operations.
 */

#include <errno.h>
#include <stdlib.h>

#include "queue.h"

// returns a pointer to the item stored at the given QueueNode
static void *queue_node_get_item(QueueNode *node) {
	return (uint8_t *)node + sizeof(QueueNode);
}

// creates an empty (count == 0) Queue object. each item is SIZE (> 0) bytes
// in size.
//
// returns -1 on error (sets errno) or 0 on success
int queue_create(Queue *queue, int size) {
	queue->count = 0;
	queue->size = size;
	queue->head = NULL;
	queue->tail = NULL;

	return 0;
}

// destroys a Queue object and frees the underlying single linked list. if an
// item destroy function DESTROY is given then it is called for each item in the
// queue (with a pointer to the item as the only parameter) before the memory
// is freed.
void queue_destroy(Queue *queue, ItemDestroyFunction destroy) {
	QueueNode *node;
	QueueNode *next;

	for (node = queue->head; node != NULL; node = next) {
		next = node->next;

		if (destroy != NULL) {
			destroy(queue_node_get_item(node));
		}

		free(node);
	}
}

// adds a new item to the tail of a Queue object. the memory of this item is
// initialized to zero.
//
// returns NULL on error (sets errno) or a pointer to the new item on success
void *queue_push(Queue *queue) {
	QueueNode *node = calloc(1, sizeof(QueueNode) + queue->size);

	if (node == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	node->next = NULL;

	if (queue->head == NULL) {
		queue->head = node;
	}

	if (queue->tail != NULL) {
		queue->tail->next = node;
	}

	queue->tail = node;

	++queue->count;

	return queue_node_get_item(node);
}

// removes an item for the head of a Queue object. if an item destroy function
// DESTROY is given then it is called (with a pointer to the item as the only
// parameter) before it is removed.
void queue_pop(Queue *queue, ItemDestroyFunction destroy) {
	QueueNode *node;

	if (queue->count == 0) {
		return;
	}

	--queue->count;

	node = queue->head;
	queue->head = node->next;

	if (queue->head == NULL) {
		queue->tail = NULL;
	}

	if (destroy != NULL) {
		destroy(queue_node_get_item(node));
	}

	free(node);
}

// returns a pointer to the item at the head of a Queue object or NULL if the
// queue is empty
void *queue_peek(Queue *queue) {
	if (queue->count == 0) {
		return NULL;
	}

	return queue_node_get_item(queue->head);
}
