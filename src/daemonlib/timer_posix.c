/*
 * daemonlib
 * Copyright (C) 2014, 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * timer_posix.c: Poll based timer implementation
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
#include <inttypes.h>
#include <poll.h>

#include "timer_posix.h"

#include "event.h"
#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void timer_handle_read(void *opaque) {
	Timer *timer = opaque;
	uint32_t configuration_id;

	if (pipe_read(&timer->notification_pipe, &configuration_id,
	              sizeof(configuration_id)) < 0) {
		log_error("Could not read from notification pipe of poll timer (handle: %d): %s (%d)",
		          timer->notification_pipe.base.read_handle,
		          get_errno_name(errno), errno);

		return;
	}

	if (configuration_id != timer->configuration_id) {
		log_debug("Ignoring timer event for mismatching configuration of poll timer (handle: %d)",
		          timer->notification_pipe.base.read_handle);

		return;
	}

	// this call might reconfigure or destroy the timer
	timer->function(timer->opaque);
}

static void timer_thread(void *opaque) {
	Timer *timer = opaque;
	bool delay_done = true;
	uint64_t delay = 0;
	uint64_t interval = 0;
	uint32_t configuration_id = timer->configuration_id;
	struct pollfd pollfd;
	int timeout;
	int ready;
	uint8_t byte;

	pollfd.fd = timer->interrupt_pipe.base.read_handle;
	pollfd.events = POLLIN;

	while (timer->running) {
		if (delay == 0 && interval == 0) {
			timeout = -1;
		} else if (!delay_done) {
			delay_done = true;

			// convert from microseconds to milliseconds
			if (delay == 0) {
				timeout = 0;
			} else if (delay < 1000) {
				timeout = 1;
			} else {
				timeout = (delay + 500) / 1000;
			}
		} else {
			// convert from microseconds to milliseconds
			if (interval == 0) {
				timeout = -1;
			} else if (interval < 1000) {
				timeout = 1;
			} else {
				timeout = (interval + 500) / 1000;
			}
		}

		ready = poll(&pollfd, 1, timeout);

		if (ready < 0) {
			if (errno_interrupted()) {
				continue;
			}

			log_debug("Could not poll on interrupt pipe of poll timer (handle: %d): %s (%d)",
			          timer->notification_pipe.base.read_handle,
			          get_errno_name(errno), errno);

			break;
		} else if (ready == 0) {
			if (pipe_write(&timer->notification_pipe, &configuration_id,
			               sizeof(configuration_id)) < 0) {
				log_error("Could not write to notification pipe of poll timer (handle: %d): %s (%d)",
				          timer->notification_pipe.base.read_handle,
				          get_errno_name(errno), errno);

				break;
			}
		} else {
			if (pipe_read(&timer->interrupt_pipe, &byte, sizeof(byte)) < 0) {
				log_error("Could not read from interrupt pipe of poll timer (handle: %d): %s (%d)",
				          timer->notification_pipe.base.read_handle,
				          get_errno_name(errno), errno);

				break;
			}

			if (!timer->running) {
				break;
			}

			delay_done = false;
			delay = timer->delay;
			interval = timer->interval;
			configuration_id = timer->configuration_id;

			semaphore_release(&timer->handshake);
		}
	}

	timer->running = false;

	semaphore_release(&timer->handshake);
}

int timer_create_(Timer *timer, TimerFunction function, void *opaque) {
	int phase = 0;

	// create notification pipe
	if (pipe_create(&timer->notification_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create interrupt pipe
	if (pipe_create(&timer->interrupt_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create interrupt pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// register notification pipe as event source
	timer->function = function;
	timer->opaque = opaque;

	if (event_add_source(timer->notification_pipe.base.read_handle,
	                     EVENT_SOURCE_TYPE_GENERIC, "timer", EVENT_READ,
	                     timer_handle_read, timer) < 0) {
		goto cleanup;
	}

	phase = 3;

	// create thread
	timer->running = true;
	timer->delay = 0;
	timer->interval = 0;
	timer->configuration_id = 0;

	semaphore_create(&timer->handshake);
	thread_create(&timer->thread, timer_thread, timer);

	log_debug("Created poll timer (handle: %d)",
	          timer->notification_pipe.base.read_handle);

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		pipe_destroy(&timer->interrupt_pipe);
		// fall through

	case 1:
		pipe_destroy(&timer->notification_pipe);
		// fall through

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void timer_destroy(Timer *timer) {
	uint8_t byte = 0;

	log_debug("Destroying poll timer (handle: %d)",
	          timer->notification_pipe.base.read_handle);

	if (timer->running) {
		timer->running = false;

		if (pipe_write(&timer->interrupt_pipe, &byte, sizeof(byte)) < 0) {
			log_error("Could not write to interrupt pipe for poll timer (handle: %d): %s (%d)",
			          timer->notification_pipe.base.read_handle,
			          get_errno_name(errno), errno);
		} else {
			thread_join(&timer->thread);
		}
	}

	event_remove_source(timer->notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);

	semaphore_destroy(&timer->handshake);

	pipe_destroy(&timer->interrupt_pipe);

	pipe_destroy(&timer->notification_pipe);
}

// setting delay and interval to 0 stops the timer
int timer_configure(Timer *timer, uint64_t delay, uint64_t interval) { // microseconds
	uint8_t byte = 0;

	if (delay > INT32_MAX) {
		log_error("Delay of %"PRIu64" microseconds is too long", delay);

		return -1;
	}

	if (interval > INT32_MAX) {
		log_error("Interval of %"PRIu64" microseconds is too long", interval);

		return -1;
	}

	if (!timer->running) {
		log_error("Thread for poll timer (handle: %d) is not running",
		          timer->notification_pipe.base.read_handle);

		return -1;
	}

	timer->delay = delay;
	timer->interval = interval;

	++timer->configuration_id;

	if (pipe_write(&timer->interrupt_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to interrupt pipe for poll timer (handle: %d): %s (%d)",
		          timer->notification_pipe.base.read_handle,
		          get_errno_name(errno), errno);

		return -1;
	}

	semaphore_acquire(&timer->handshake);

	if (!timer->running) {
		log_error("Thread for poll timer (handle: %d) exited due to an error",
		          timer->notification_pipe.base.read_handle);

		return -1;
	}

	return 0;
}
