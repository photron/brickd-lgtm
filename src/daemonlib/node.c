/*
 * daemonlib
 * Copyright (C) 2014, 2016 Matthias Bolte <matthias@tinkerforge.com>
 *
 * node.c: Double linked list helpers
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

#include "node.h"

void node_reset(Node *node) {
	node->prev = node;
	node->next = node;
}

// NOTE: assumes that INSERT is not part of another linked list already
void node_insert_before(Node *node, Node *insert) {
	insert->prev = node->prev;
	node->prev = insert;
	insert->next = insert->prev->next;
	insert->prev->next = insert;
}

// NOTE: assumes that INSERT is not part of another linked list already
void node_insert_after(Node *node, Node *insert) {
	insert->next = node->next;
	node->next = insert;
	insert->prev = insert->next->prev;
	insert->next->prev = insert;
}

void node_remove(Node *node) {
	node->next->prev = node->prev;
	node->prev->next = node->next;

	node_reset(node);
}
