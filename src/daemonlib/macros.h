/*
 * daemonlib
 * Copyright (C) 2012-2014, 2016, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * macros.h: Preprocessor macros
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

#ifndef DAEMONLIB_MACROS_H
#define DAEMONLIB_MACROS_H

#include <stddef.h>

#ifdef __clang__
	#if __has_feature(c_static_assert)
		#define STATIC_ASSERT(condition, message) _Static_assert(condition, message)
	#else
		#define STATIC_ASSERT(condition, message) // FIXME
	#endif
	#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) // FIXME
#elif defined __GNUC__
	#ifndef __GNUC_PREREQ
		#define __GNUC_PREREQ(major, minor) ((((__GNUC__) << 16) + (__GNUC_MINOR__)) >= (((major) << 16) + (minor)))
	#endif
	#if __GNUC_PREREQ(4, 4)
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) __attribute__((__format__(__gnu_printf__, fmtpos, argpos)))
	#else
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) __attribute__((__format__(__printf__, fmtpos, argpos)))
	#endif
	#if __GNUC_PREREQ(4, 6)
		#define STATIC_ASSERT(condition, message) _Static_assert(condition, message)
	#else
		#define STATIC_ASSERT(condition, message) // FIXME
	#endif
#else
	#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) // FIXME
	#define STATIC_ASSERT(condition, message) // FIXME
#endif

// if __GNUC_PREREQ is not defined by now then define it to always be false
#ifndef __GNUC_PREREQ
	#define __GNUC_PREREQ(major, minor) 0
#endif

#ifndef ABS
	#define ABS(a) (((a) < 0) ? (-(a)) : (a))
#endif
#ifndef MIN
	#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
	#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// round SIZE up to the next multiple of 16. the calculation relies on SIZE
// being a signed int. with float the division would not truncate the result.
// with unsigned int SIZE - 1 would overflow to a big value if size is 0.
#define GROW_ALLOCATION(size) ((((int)(size) - 1) / 16 + 1) * 16)

// this is intentionally called containerof instead of container_of to avoid
// conflicts with potential other definitions of the container_of macro
#ifdef __GNUC__
	#define containerof(ptr, type, member) ({ \
		const __typeof__(((type *)0)->member) *__ptr = ptr; \
		(type *)((char *)__ptr - offsetof(type, member)); })
#else
	#define containerof(ptr, type, member) \
		((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#endif // DAEMONLIB_MACROS_H
