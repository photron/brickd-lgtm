/*
 * daemonlib
 * Copyright (C) 2012-2015, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event_posix.c: poll based event loop
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

#include "event.h"

#include "array.h"
#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

int event_init_platform(void) {
	return 0;
}

void event_exit_platform(void) {
}

int event_source_added_platform(EventSource *event_source) {
	(void)event_source;

	return 0;
}

int event_source_modified_platform(EventSource *event_source) {
	(void)event_source;

	return 0;
}

void event_source_removed_platform(EventSource *event_source) {
	(void)event_source;
}

int event_run_platform(Array *event_sources, bool *running, EventCleanupFunction cleanup) {
	int result = -1;
	Array pollfds;
	int i;
	EventSource *event_source;
	struct pollfd *pollfd;
	int ready;
	int handled;

	// create pollfd array
	if (array_create(&pollfds, 32, sizeof(struct pollfd), true) < 0) {
		log_error("Could not create pollfd array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	*running = true;

	cleanup();
	event_cleanup_sources();

	while (*running) {
		// update pollfd array
		if (array_resize(&pollfds, event_sources->count, NULL) < 0) {
			log_error("Could not resize pollfd array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		for (i = 0; i < event_sources->count; ++i) {
			event_source = array_get(event_sources, i);
			pollfd = array_get(&pollfds, i);

			pollfd->fd = event_source->handle;
			pollfd->events = event_source->events;
			pollfd->revents = 0;
		}

		// start to poll
		log_event_debug("Starting to poll on %d event source(s)", pollfds.count);

		ready = poll((struct pollfd *)pollfds.bytes, pollfds.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("Poll got interrupted");

				continue;
			}

			log_error("Count not poll on event source(s): %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		// handle poll result
		log_event_debug("Poll returned %d event source(s) as ready", ready);

		handled = 0;

		// this loop assumes that event source array and pollfd array can be
		// matched by index. this means that the first N items of the event
		// source array (with N = items in pollfd array) are not removed
		// or replaced during the iteration over the pollfd array. because
		// of this event_remove_source only marks event sources as removed,
		// the actual removal is done after this loop by event_cleanup_sources
		for (i = 0; *running && i < pollfds.count && ready > handled; ++i) {
			pollfd = array_get(&pollfds, i);

			if (pollfd->revents == 0) {
				continue;
			}

			event_handle_source(array_get(event_sources, i), pollfd->revents);

			++handled;
		}

		if (ready == handled) {
			log_event_debug("Handled all ready event sources");
		} else if (*running) {
			log_warn("Handled only %d of %d ready event source(s)",
			         handled, ready);
		}

		// now cleanup event sources that got marked as disconnected/removed
		// during the event handling
		cleanup();
		event_cleanup_sources();
	}

	result = 0;

cleanup:
	*running = false;

	array_destroy(&pollfds, NULL);

	return result;
}
