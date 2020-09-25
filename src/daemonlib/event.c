/*
 * daemonlib
 * Copyright (C) 2012-2015, 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * event.c: Event specific functions
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
#include <string.h>

#include "event.h"

#include "array.h"
#include "log.h"
#include "pipe.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static bool _running;
static bool _stop_requested;
static Array _event_sources;
static Pipe _stop_pipe;

extern int event_init_platform(void);
extern void event_exit_platform(void);
extern int event_source_added_platform(EventSource *event_source);
extern int event_source_modified_platform(EventSource *event_source);
extern void event_source_removed_platform(EventSource *event_source);
extern int event_run_platform(Array *sources, bool *running,
                              EventCleanupFunction cleanup);

const char *event_get_source_type_name(EventSourceType type, bool upper) {
	switch (type) {
	case EVENT_SOURCE_TYPE_GENERIC: return upper ? "Generic" : "generic";
	case EVENT_SOURCE_TYPE_USB:     return "USB";

	default:                        return upper ? "<Unknown>" : "<unknown>";
	}
}

int event_init(void) {
	int phase = 0;

	log_debug("Initializing event subsystem");

	_running = false;
	_stop_requested = false;

	// create event source array, the EventSource struct is not relocatable
	// because epoll might store a pointer to it
	if (array_create(&_event_sources, 32, sizeof(EventSource), false) < 0) {
		log_error("Could not create event source array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_init_platform() < 0) {
		goto cleanup;
	}

	phase = 2;

	// create stop pipe
	if (pipe_create(&_stop_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (event_add_source(_stop_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "event-stop", EVENT_READ, NULL, NULL) < 0) {
		goto cleanup;
	}

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		pipe_destroy(&_stop_pipe);
		// fall through

	case 2:
		event_exit_platform();
		// fall through

	case 1:
		array_destroy(&_event_sources, NULL);
		// fall through

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void event_exit(void) {
	int i;
	EventSource *event_source;

	log_debug("Shutting down event subsystem");

	event_remove_source(_stop_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_stop_pipe);

	event_exit_platform();

	event_cleanup_sources();

	for (i = 0; i < _event_sources.count; ++i) {
		event_source = array_get(&_event_sources, i);

		log_warn("Leaking %s event source (handle: %d, name: %s, events: 0x%04X) at index %d",
		         event_get_source_type_name(event_source->type, false),
		         event_source->handle, event_source->name, event_source->events, i);
	}

	array_destroy(&_event_sources, NULL);
}

static EventSource *event_find_source(int start, int end, IOHandle handle,
                                      EventSourceType type, int *index) {
	int i;
	int step = start < end ? 1 : -1;
	EventSource *event_source;

	for (i = start; i != end; i += step) {
		event_source = array_get(&_event_sources, i);

		if (event_source->handle == handle && event_source->type == type) {
			if (index != NULL) {
				*index = i;
			}

			return event_source;
		}
	}

	return NULL;
}

// the event sources array contains tuples (handle, type). each tuple can be
// in the array only once. trying to add (5, USB) to the array while such a
// tuple is already in the array is an error. there is one exception from this
// rule: if a tuple got marked as removed, it is allowed to re-add it even
// before event_cleanup_sources was called to really remove the tuples that
// got marked as removed before
int event_add_source(IOHandle handle, EventSourceType type, const char *name,
                     uint32_t events, EventFunction function, void *opaque) {
	int index;
	EventSource *event_source;
	EventSource backup;

	event_source = event_find_source(0, _event_sources.count, handle, type, &index);

	if (event_source != NULL) {
		// readd removed event source
		if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
			memcpy(&backup, event_source, sizeof(backup));

			event_source->name = name;
			event_source->events = events;
			event_source->state = EVENT_SOURCE_STATE_READDED;

			if ((events & EVENT_READ) != 0) {
				event_source->read = function;
				event_source->read_opaque = opaque;
			}

			if ((events & EVENT_WRITE) != 0) {
				event_source->write = function;
				event_source->write_opaque = opaque;
			}

			if ((events & EVENT_PRIO) != 0) {
				event_source->prio = function;
				event_source->prio_opaque = opaque;
			}

			if ((events & EVENT_ERROR) != 0) {
				event_source->error = function;
				event_source->error_opaque = opaque;
			}

			if (event_source_added_platform(event_source) < 0) {
				memcpy(event_source, &backup, sizeof(backup));

				return -1;
			}

			log_event_debug("Readded %s event source (handle: %d, name: %s) at index %d",
			                event_get_source_type_name(type, false), handle, name, index);

			return 0;
		}

		log_error("%s event source (handle: %d, name: %s) already added at index %d",
		          event_get_source_type_name(event_source->type, true),
		          event_source->handle, event_source->name, index);

		return -1;
	} else {
		// add new event source
		event_source = array_append(&_event_sources);

		if (event_source == NULL) {
			log_error("Could not append to event source array: %s (%d)",
			          get_errno_name(errno), errno);

			return -1;
		}

		event_source->handle = handle;
		event_source->type = type;
		event_source->name = name;
		event_source->events = events;
		event_source->state = EVENT_SOURCE_STATE_ADDED;

		if ((events & EVENT_READ) != 0) {
			event_source->read = function;
			event_source->read_opaque = opaque;
		}

		if ((events & EVENT_WRITE) != 0) {
			event_source->write = function;
			event_source->write_opaque = opaque;
		}

		if ((events & EVENT_PRIO) != 0) {
			event_source->prio = function;
			event_source->prio_opaque = opaque;
		}

		if ((events & EVENT_ERROR) != 0) {
			event_source->error = function;
			event_source->error_opaque = opaque;
		}

		if (event_source_added_platform(event_source) < 0) {
			array_remove(&_event_sources, _event_sources.count - 1, NULL);

			return -1;
		}

		log_event_debug("Added %s event source (handle: %d, name: %s, events: 0x%04X) at index %d",
		                event_get_source_type_name(type, false),
		                handle, name, events, _event_sources.count - 1);

		return 0;
	}
}

// the events that an event source was added for can be modified
int event_modify_source(IOHandle handle, EventSourceType type, uint32_t events_to_remove,
                        uint32_t events_to_add, EventFunction function, void *opaque) {
	int index;
	EventSource *event_source;
	EventSource backup;

	event_source = event_find_source(0, _event_sources.count, handle, type, &index);

	if (event_source == NULL) {
		log_warn("Could not modify unknown %s event source (handle: %d)",
		         event_get_source_type_name(type, false), handle);

		return -1;
	}

	if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
		log_error("Cannot modify removed %s event source (handle: %d, name: %s) at index %d",
		          event_get_source_type_name(type, false), event_source->handle,
		          event_source->name, index);

		return -1;
	}

	memcpy(&backup, event_source, sizeof(backup));

	// modify events bitmask
	if ((event_source->events & events_to_remove) != events_to_remove) {
		log_warn("Events to be removed (0x%04X) from %s event source (handle: %d, name: %s) at index %d were not added before",
		         events_to_remove, event_get_source_type_name(type, false),
		         event_source->handle, event_source->name, index);
	}

	event_source->events &= ~events_to_remove;

	if ((event_source->events & events_to_add) != 0) {
		log_warn("Events to be added (0x%04X) to %s event source (handle: %d, name: %s) at index %d are already added",
		         events_to_add, event_get_source_type_name(type, false),
		         event_source->handle, event_source->name, index);
	}

	event_source->events |= events_to_add;

	// unset functions for removed events
	if ((events_to_remove & EVENT_READ) != 0) {
		event_source->read = NULL;
		event_source->read_opaque = NULL;
	}

	if ((events_to_remove & EVENT_WRITE) != 0) {
		event_source->write = NULL;
		event_source->write_opaque = NULL;
	}

	if ((events_to_remove & EVENT_PRIO) != 0) {
		event_source->prio = NULL;
		event_source->prio_opaque = NULL;
	}

	if ((events_to_remove & EVENT_ERROR) != 0) {
		event_source->error = NULL;
		event_source->error_opaque = NULL;
	}

	// set functions for added events
	if ((events_to_add & EVENT_READ) != 0) {
		event_source->read = function;
		event_source->read_opaque = opaque;
	}

	if ((events_to_add & EVENT_WRITE) != 0) {
		event_source->write = function;
		event_source->write_opaque = opaque;
	}

	if ((events_to_add & EVENT_PRIO) != 0) {
		event_source->prio = function;
		event_source->prio_opaque = opaque;
	}

	if ((events_to_add & EVENT_ERROR) != 0) {
		event_source->error = function;
		event_source->error_opaque = opaque;
	}

	event_source->state = EVENT_SOURCE_STATE_MODIFIED;

	if (event_source_modified_platform(event_source) < 0) {
		memcpy(event_source, &backup, sizeof(backup));

		return -1;
	}

	log_event_debug("Modified (removed: 0x%04X, added: 0x%04X) %s event source (handle: %d, name: %s) at index %d",
	                events_to_remove, events_to_add,
	                event_get_source_type_name(type, false), event_source->handle,
	                event_source->name, index);

	return 0;
}

// only mark event sources as removed here, because the event loop might
// be in the middle of iterating the event sources array when this function
// is called
void event_remove_source(IOHandle handle, EventSourceType type) {
	int index;
	EventSource *event_source;

	// iterate backwards to remove the last added instance of an event source,
	// otherwise an remove-add-remove sequence for the same event source between
	// two calls to event_cleanup_sources doesn't work properly
	event_source = event_find_source(_event_sources.count - 1, -1, handle, type, &index);

	if (event_source == NULL) {
		log_warn("Could not mark unknown %s event source (handle: %d) as removed",
		         event_get_source_type_name(type, false), handle);

		return;
	}

	if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
		log_warn("%s event source (handle: %d, name: %s, events: 0x%04X) already marked as removed at index %d",
		         event_get_source_type_name(event_source->type, true),
		         event_source->handle, event_source->name, event_source->events, index);
	} else {
		event_source->state = EVENT_SOURCE_STATE_REMOVED;

		event_source_removed_platform(event_source);

		log_event_debug("Marked %s event source (handle: %d, name: %s, events: 0x%04X) as removed at index %d",
		                event_get_source_type_name(event_source->type, false),
		                event_source->handle, event_source->name,
		                event_source->events, index);
	}
}

// remove event sources that got marked as removed and mark (re-)added event
// sources as normal
void event_cleanup_sources(void) {
	int i;
	EventSource *event_source;

	// iterate backwards for simpler index handling and to be able to print
	// the correct index
	for (i = _event_sources.count - 1; i >= 0; --i) {
		event_source = array_get(&_event_sources, i);

		if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
			log_event_debug("Removed %s event source (handle: %d, name: %s, events: 0x%04X) at index %d",
			                event_get_source_type_name(event_source->type, false),
			                event_source->handle, event_source->name,
			                event_source->events, i);

			array_remove(&_event_sources, i, NULL);
		} else {
			event_source->state = EVENT_SOURCE_STATE_NORMAL;
		}
	}
}

void event_handle_source(EventSource *event_source, uint32_t received_events) {
	if (event_source->state != EVENT_SOURCE_STATE_NORMAL) {
		log_event_debug("Ignoring %s event source (handle: %d, name: %s, received-events: 0x%04X) in state transition",
		                event_get_source_type_name(event_source->type, false),
		                event_source->handle, event_source->name, received_events);

		return;
	}

	log_event_debug("Handling %s event source (handle: %d, name: %s, received-events: 0x%04X)",
	                event_get_source_type_name(event_source->type, false),
	                event_source->handle, event_source->name, received_events);

	// Here we currently only check if prio and error or read and write have
	// the same functions. Currently read/write and prio/error are not mixed.
	// It is probably OK to leave it this way since they never seem to be used
	// together. For example: On a sysfs gpio value file you can only use
	// prio/error, while on an eventfd or similar prio/error can't be used.
	if (event_source->prio != NULL &&
	    event_source->prio == event_source->error &&
	    event_source->prio_opaque == event_source->error_opaque) {
		// prio and error event function are the same, don't call it twice,
		// only call the prio event function once
		if ((received_events & (EVENT_PRIO | EVENT_ERROR)) != 0) {
			event_source->prio(event_source->prio_opaque);
		}
	} else if (event_source->read != NULL &&
	           event_source->read == event_source->write &&
	           event_source->read_opaque == event_source->write_opaque) {
		// read and write event function are the same, don't call it twice,
		// only call the read event function once
		if ((received_events & (EVENT_READ | EVENT_WRITE)) != 0) {
			event_source->read(event_source->read_opaque);
		}
	} else {
		if ((received_events & EVENT_READ) != 0 && event_source->read != NULL) {
			event_source->read(event_source->read_opaque);
		}

		if ((received_events & EVENT_WRITE) != 0 && event_source->write != NULL) {
			// if the event source got removed in the meantime then don't deliver
			// the write event anymore
			if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
				log_debug("Ignoring removed %s event source (handle: %d, name: %s, received-events: 0x%04X)",
				          event_get_source_type_name(event_source->type, false),
				          event_source->handle, event_source->name, received_events);

				return;
			}

			event_source->write(event_source->write_opaque);
		}

		if ((received_events & EVENT_PRIO) != 0 && event_source->prio != NULL) {
			// if the event source got removed in the meantime then don't deliver
			// the write event anymore
			if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
				log_debug("Ignoring removed %s event source (handle: %d, name: %s, received-events: 0x%04X)",
				          event_get_source_type_name(event_source->type, false),
				          event_source->handle, event_source->name, received_events);

				return;
			}

			event_source->prio(event_source->prio_opaque);
		}

		if ((received_events & EVENT_ERROR) != 0 && event_source->error != NULL) {
			// if the event source got removed in the meantime then don't deliver
			// the write event anymore
			if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
				log_debug("Ignoring removed %s event source (handle: %d, name: %s, received-events: 0x%04X)",
				          event_get_source_type_name(event_source->type, false),
				          event_source->handle, event_source->name, received_events);

				return;
			}

			event_source->error(event_source->error_opaque);
		}
	}
}

int event_run(EventCleanupFunction cleanup) {
	int rc;

	if (_running) {
		log_warn("Event loop already running");

		return 0;
	}

	if (_stop_requested) {
		log_debug("Not starting the event loop, stop was requested");

		return 0;
	}

	log_debug("Starting the event loop");

	rc = event_run_platform(&_event_sources, &_running, cleanup);

	if (rc < 0) {
		log_error("Event loop aborted");
	} else {
		log_debug("Event loop stopped");
	}

	return rc;
}

// might be called from a non-main-thread
void event_stop(void) {
	uint8_t byte = 0;

	_stop_requested = true;

	if (!_running) {
		return;
	}

	_running = false;

	// write to the stop pipe to wake the event loop up to make it recognize
	// the stop request
	if (pipe_write(&_stop_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	log_debug("Stopping the event loop");
}
