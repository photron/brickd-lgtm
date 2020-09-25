/*
 * daemonlib
 * Copyright (C) 2014, 2016-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * timer_winapi.c: WinAPI based timer implementation
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

#include "timer_winapi.h"

#include "event.h"
#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void timer_handle_read(void *opaque) {
	Timer *timer = opaque;
	uint32_t configuration_id;

	if (pipe_read(&timer->notification_pipe, &configuration_id,
	              sizeof(configuration_id)) < 0) {
		log_error("Could not read from notification pipe of waitable timer (handle: %p): %s (%d)",
		          timer->waitable_timer, get_errno_name(errno), errno);

		return;
	}

	if (configuration_id != timer->configuration_id) {
		log_debug("Ignoring timer event for mismatching configuration of waitable timer (handle: %p)",
		          timer->waitable_timer);

		return;
	}

	// this call might reconfigure or destroy the timer
	timer->function(timer->opaque);
}

static void timer_thread(void *opaque) {
	Timer *timer = opaque;
	HANDLE handles[2];
	int rc;
	uint32_t configuration_id = timer->configuration_id;
	LARGE_INTEGER delay;
	LONG interval;

	handles[0] = timer->waitable_timer;
	handles[1] = timer->interrupt_event;

	delay.QuadPart = 0;
	interval = 0;

	while (timer->running) {
		rc = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

		if (rc == WAIT_OBJECT_0) { // timer
			if (delay.QuadPart == 0 && interval == 0) {
				log_debug("Ignoring timer event for inactive waitable timer (handle: %p)",
				          timer->waitable_timer);

				continue;
			}

			if (pipe_write(&timer->notification_pipe, &configuration_id,
			               sizeof(configuration_id)) < 0) {
				log_error("Could not write to notification pipe of waitable timer (handle: %p): %s (%d)",
				          timer->waitable_timer, get_errno_name(errno), errno);

				break;
			}
		} else if (rc == WAIT_OBJECT_0 + 1) { // interrupt
			if (!timer->running) {
				break;
			}

			if (timer->delay == 0 && timer->interval == 0) {
				delay.QuadPart = 0;
				interval = 0;

				if (!CancelWaitableTimer(timer->waitable_timer)) {
					rc = ERRNO_WINAPI_OFFSET + GetLastError();

					log_error("Could not cancel waitable timer (handle: %p): %s (%d)",
					          timer->waitable_timer, get_errno_name(rc), rc);

					break;
				}
			} else {
				// convert from microseconds to 100 nanoseconds.
				// negate to get relative timer
				delay.QuadPart = timer->delay * -10;

				// convert from microseconds to milliseconds
				if (timer->interval == 0) {
					interval = 0;
				} else if (timer->interval < 1000) {
					interval = 1;
				} else {
					interval = (LONG)((timer->interval + 500) / 1000);
				}

				if (!SetWaitableTimer(timer->waitable_timer, &delay, interval,
				                      NULL, NULL, FALSE)) {
					rc = ERRNO_WINAPI_OFFSET + GetLastError();

					log_error("Could not configure waitable timer (handle: %p): %s (%d)",
					          timer->waitable_timer, get_errno_name(rc), rc);

					break;
				}
			}

			configuration_id = timer->configuration_id;

			semaphore_release(&timer->handshake);
		} else {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not wait for timer/interrupt event of waitable timer (handle: %p): %s (%d)",
			          timer->waitable_timer, get_errno_name(rc), rc);

			break;
		}
	}

	timer->running = false;

	semaphore_release(&timer->handshake);
}

int timer_create_(Timer *timer, TimerFunction function, void *opaque) {
	int phase = 0;
	int rc;

	// create notification pipe
	if (pipe_create(&timer->notification_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create waitable timer
	timer->waitable_timer = CreateWaitableTimer(NULL, FALSE, NULL);

	if (timer->waitable_timer == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create waitable timer: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 2;

	// create interrupt event
	timer->interrupt_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (timer->interrupt_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	// register notification pipe as event source
	timer->function = function;
	timer->opaque = opaque;

	if (event_add_source(timer->notification_pipe.base.read_handle,
	                     EVENT_SOURCE_TYPE_GENERIC, "timer", EVENT_READ,
	                     timer_handle_read, timer) < 0) {
		goto cleanup;
	}

	phase = 4;

	// create thread
	timer->running = true;
	timer->delay = 0;
	timer->interval = 0;
	timer->configuration_id = 0;

	semaphore_create(&timer->handshake);
	thread_create(&timer->thread, timer_thread, timer);

	log_debug("Created waitable timer (handle: %p)", timer->waitable_timer);

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		CloseHandle(timer->interrupt_event);
		// fall through

	case 2:
		CloseHandle(timer->waitable_timer);
		// fall through

	case 1:
		pipe_destroy(&timer->notification_pipe);
		// fall through

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void timer_destroy(Timer *timer) {
	int rc;

	log_debug("Destroying waitable timer (handle: %p)", timer->waitable_timer);

	if (timer->running) {
		timer->running = false;

		if (!SetEvent(timer->interrupt_event)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not interrupt thread for waitable timer (handle: %p): %s (%d)",
			          timer->waitable_timer, get_errno_name(rc), rc);
		} else {
			thread_join(&timer->thread);
		}
	}

	event_remove_source(timer->notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);

	semaphore_destroy(&timer->handshake);

	CloseHandle(timer->interrupt_event);

	CloseHandle(timer->waitable_timer);

	pipe_destroy(&timer->notification_pipe);
}

// setting delay and interval to 0 stops the timer
int timer_configure(Timer *timer, uint64_t delay, uint64_t interval) { // microseconds
	int rc;

	if (!timer->running) {
		log_error("Thread for waitable timer (handle: %p) is not running",
		          timer->waitable_timer);

		return -1;
	}

	timer->delay = delay;
	timer->interval = interval;

	++timer->configuration_id;

	if (!SetEvent(timer->interrupt_event)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not interrupt thread for waitable timer (handle: %p): %s (%d)",
		          timer->waitable_timer, get_errno_name(rc), rc);

		return -1;
	}

	semaphore_acquire(&timer->handshake);

	if (!timer->running) {
		log_error("Thread for waitable timer (handle: %p) exited due to an error",
		          timer->waitable_timer);

		return -1;
	}

	return 0;
}
