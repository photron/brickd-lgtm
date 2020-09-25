/*
 * daemonlib
 * Copyright (C) 2012, 2014, 2018 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * event.h: Event specific functions
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

#ifndef DAEMONLIB_EVENT_H
#define DAEMONLIB_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#ifndef _WIN32
	#if defined __linux__ && defined DAEMONLIB_WITH_EPOLL
		#include <sys/epoll.h>
	#else
		#include <poll.h>
	#endif
#endif

#include "io.h"

typedef void (*EventFunction)(void *opaque);
typedef void (*EventCleanupFunction)(void);

typedef enum { // bitmask
#ifdef _WIN32
	EVENT_READ  = 0x0001,
	EVENT_WRITE = 0x0004,
	EVENT_PRIO  = 0x0002,
	EVENT_ERROR = 0x0008
#else
	#if defined __linux__ && defined DAEMONLIB_WITH_EPOLL
		EVENT_READ  = EPOLLIN,
		EVENT_WRITE = EPOLLOUT,
		EVENT_PRIO  = EPOLLPRI,
		EVENT_ERROR = EPOLLERR
	#else
		EVENT_READ  = POLLIN,
		EVENT_WRITE = POLLOUT,
		EVENT_PRIO  = POLLPRI,
		EVENT_ERROR = POLLERR
	#endif
#endif
} Event;

typedef enum {
	EVENT_SOURCE_TYPE_GENERIC = 0,
	EVENT_SOURCE_TYPE_USB
} EventSourceType;

typedef enum {
	EVENT_SOURCE_STATE_NORMAL = 0,
	EVENT_SOURCE_STATE_ADDED,
	EVENT_SOURCE_STATE_REMOVED,
	EVENT_SOURCE_STATE_READDED,
	EVENT_SOURCE_STATE_MODIFIED
} EventSourceState;

typedef struct {
	IOHandle handle;
	EventSourceType type;
	const char *name;
	uint32_t events;
	EventSourceState state;
	EventFunction read;
	void *read_opaque;
	EventFunction write;
	void *write_opaque;
	EventFunction prio;
	void *prio_opaque;
	EventFunction error;
	void *error_opaque;
} EventSource;

const char *event_get_source_type_name(EventSourceType type, bool upper);

int event_init(void);
void event_exit(void);

int event_add_source(IOHandle handle, EventSourceType type, const char *name,
                     uint32_t events, EventFunction function, void *opaque);
int event_modify_source(IOHandle handle, EventSourceType type, uint32_t events_to_remove,
                        uint32_t events_to_add, EventFunction function, void *opaque);
void event_remove_source(IOHandle handle, EventSourceType type);
void event_cleanup_sources(void);

void event_handle_source(EventSource *event_source, uint32_t received_events);

int event_run(EventCleanupFunction cleanup);
void event_stop(void);

#endif // DAEMONLIB_EVENT_H
