/*
 * daemonlib
 * Copyright (C) 2014, 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * signal.c: Signal specific functions
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
#include <signal.h>

#include "signal.h"

#include "event.h"
#include "log.h"
#include "pipe.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _signal_pipe;
static SIGHUPFunction _handle_sighup = NULL;
static SIGUSR1Function _handle_sigusr1 = NULL;

static void signal_handle(void *opaque) {
	int signal_number;

	(void)opaque;

	if (pipe_read(&_signal_pipe, &signal_number, sizeof(signal_number)) < 0) {
		log_error("Could not read from signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	if (signal_number == SIGINT) {
		log_info("Received SIGINT");

		event_stop();
	} else if (signal_number == SIGTERM) {
		log_info("Received SIGTERM");

		event_stop();
	} else if (signal_number == SIGHUP) {
		log_info("Received SIGHUP");

		if (_handle_sighup != NULL) {
			_handle_sighup();
		}
	} else if (signal_number == SIGUSR1) {
		log_info("Received SIGUSR1");

		if (_handle_sigusr1 != NULL) {
			_handle_sigusr1();
		}
	} else {
		log_warn("Received unexpected signal %d", signal_number);
	}
}

static void signal_forward(int signal_number) {
	// need to forward signal here with async-safe functions only
	pipe_write(&_signal_pipe, &signal_number, sizeof(signal_number));
}

int signal_init(SIGHUPFunction sighup, SIGUSR1Function sigusr1) {
	int phase = 0;

	_handle_sighup = sighup;
	_handle_sigusr1 = sigusr1;

	// create signal pipe
	if (pipe_create(&_signal_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_signal_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "signal", EVENT_READ, signal_handle, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// handle SIGINT to stop the event loop
	if (signal(SIGINT, signal_forward) == SIG_ERR) {
		log_error("Could not install signal handler for SIGINT: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	// handle SIGTERM to stop the event loop
	if (signal(SIGTERM, signal_forward) == SIG_ERR) {
		log_error("Could not install signal handler for SIGTERM: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 4;

	// ignore SIGPIPE to make socket functions report EPIPE in case of broken pipes
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		log_error("Could not ignore SIGPIPE signal: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 5;

	// handle SIGHUP to call a user provided function
	if (signal(SIGHUP, signal_forward) == SIG_ERR) {
		log_error("Could not install signal handler for SIGHUP: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 6;

	// handle SIGUSR1 to call a user provided function
	if (signal(SIGUSR1, signal_forward) == SIG_ERR) {
		log_error("Could not install signal handler for SIGUSR1: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 6:
		signal(SIGHUP, SIG_DFL);
		// fall through

	case 5:
		signal(SIGPIPE, SIG_DFL);
		// fall through

	case 4:
		signal(SIGTERM, SIG_DFL);
		// fall through

	case 3:
		signal(SIGINT, SIG_DFL);
		// fall through

	case 2:
		event_remove_source(_signal_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 1:
		pipe_destroy(&_signal_pipe);
		// fall through

	default:
		break;
	}

	return phase == 7 ? 0 : -1;
}

void signal_exit(void) {
	signal(SIGUSR1, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);

	event_remove_source(_signal_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_signal_pipe);
}
