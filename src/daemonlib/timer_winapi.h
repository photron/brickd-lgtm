/*
 * daemonlib
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * timer_winapi.h: WinAPI based timer implementation
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

#ifndef DAEMONLIB_TIMER_WINAPI_H
#define DAEMONLIB_TIMER_WINAPI_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "io.h"
#include "pipe.h"
#include "threads.h"

typedef void (*TimerFunction)(void *opaque);

typedef struct {
	Pipe notification_pipe;
	HANDLE waitable_timer;
	HANDLE interrupt_event;
	Semaphore handshake;
	Thread thread;
	bool running;
	uint64_t delay; // in microseconds
	uint64_t interval; // in microseconds
	uint32_t configuration_id;
	TimerFunction function;
	void *opaque;
} Timer;

#endif // DAEMONLIB_TIMER_WINAPI_H
