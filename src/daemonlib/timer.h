/*
 * daemonlib
 * Copyright (C) 2014, 2016, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * timer.h: Timer specific functions
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

#ifndef DAEMONLIB_TIMER_H
#define DAEMONLIB_TIMER_H

#include <stdint.h>

#ifdef DAEMONLIB_UWP_BUILD
	#include "timer_uwp.h"
#elif defined _WIN32
	#include "timer_winapi.h"
#elif defined __linux__ && !defined __ANDROID__
	#include "timer_linux.h"
#else
	#include "timer_posix.h"
#endif

int timer_create_(Timer *timer, TimerFunction function, void *opaque);
void timer_destroy(Timer *timer);

int timer_configure(Timer *timer, uint64_t delay, uint64_t interval); // microseconds

#endif // DAEMONLIB_TIMER_H
