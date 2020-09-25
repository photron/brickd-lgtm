/*
 * daemonlib
 * Copyright (C) 2014-2016, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event_linux.c: epoll based event loop
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

#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "event.h"

#include "array.h"
#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static int _epollfd;
static int _epollfd_event_count;

int event_init_platform(void) {
	// create epollfd
	_epollfd = epoll_create1(EPOLL_CLOEXEC);

	if (_epollfd < 0) {
		log_error("Could not create epollfd: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	_epollfd_event_count = 0;

	return 0;
}

void event_exit_platform(void) {
	robust_close(_epollfd); // FIXME: remove remaining events (if any) from epollfd?
}

int event_source_added_platform(EventSource *event_source) {
	struct epoll_event event;

	memset(&event, 0, sizeof(event));

	event.events = event_source->events;
	event.data.ptr = event_source;

	if (epoll_ctl(_epollfd, EPOLL_CTL_ADD, event_source->handle, &event) < 0) {
		log_error("Could not add %s event source (handle: %d) to epollfd: %s (%d)",
		          event_get_source_type_name(event_source->type, false),
		          event_source->handle, get_errno_name(errno), errno);

		return -1;
	}

	++_epollfd_event_count;

	return 0;
}

int event_source_modified_platform(EventSource *event_source) {
	struct epoll_event event;

	event.events = event_source->events;
	event.data.ptr = event_source;

	if (epoll_ctl(_epollfd, EPOLL_CTL_MOD, event_source->handle, &event) < 0) {
		log_error("Could not modify %s event source (handle: %d) added to epollfd: %s (%d)",
		          event_get_source_type_name(event_source->type, false),
		          event_source->handle, get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void event_source_removed_platform(EventSource *event_source) {
	struct epoll_event event;

	event.events = event_source->events;
	event.data.ptr = event_source;

	if (epoll_ctl(_epollfd, EPOLL_CTL_DEL, event_source->handle, &event) < 0) {
		log_error("Could not remove %s event source (handle: %d) from epollfd: %s (%d)",
		          event_get_source_type_name(event_source->type, false),
		          event_source->handle, get_errno_name(errno), errno);

		return;
	}

	--_epollfd_event_count;
}

int event_run_platform(Array *event_sources, bool *running, EventCleanupFunction cleanup) {
	int result = -1;
	int i;
	EventSource *event_source;
	Array received_events;
	struct epoll_event *received_event;
	int ready;

	(void)event_sources;

	if (array_create(&received_events, 32, sizeof(struct epoll_event), true) < 0) {
		log_error("Could not create epoll event array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	*running = true;

	cleanup();
	event_cleanup_sources();

	while (*running) {
		if (array_resize(&received_events, _epollfd_event_count, NULL) < 0) {
			log_error("Could not resize pollfd array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		// start to epoll
		log_event_debug("Starting to epoll on %d event source(s)",
		                _epollfd_event_count);

		ready = epoll_wait(_epollfd, (struct epoll_event *)received_events.bytes,
		                   received_events.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("EPoll got interrupted");

				continue;
			}

			log_error("Count not epoll on event source(s): %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		// handle poll result
		log_event_debug("EPoll returned %d event source(s) as ready", ready);

		// this loop assumes that event sources stored in the epoll events
		// are valid. because of this event_remove_source only marks event
		// sources as removed, the actual removal is done after this loop
		// by event_cleanup_sources
		for (i = 0; *running && i < ready; ++i) {
			received_event = array_get(&received_events, i);
			event_source = received_event->data.ptr;

			event_handle_source(event_source, received_event->events);
		}

		log_event_debug("Handled all ready event sources");

		// now cleanup event sources that got marked as disconnected/removed
		// during the event handling
		cleanup();
		event_cleanup_sources();
	}

	result = 0;

cleanup:
	*running = false;

	array_destroy(&received_events, NULL);

	return result;
}
