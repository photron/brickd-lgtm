/*
 * daemonlib
 * Copyright (C) 2014, 2016-2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * file.c: File based I/O device
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
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
	#include <io.h>
#else
	#include <unistd.h>
#endif

#include "file.h"

#include "utils.h"

// sets errno on error
int file_create(File *file, const char *name, int flags, int mode) { // takes open flags
#ifndef _WIN32
	int fcntl_flags;
#endif

	if (io_create(&file->base, "file",
	              (IODestroyFunction)file_destroy,
	              (IOReadFunction)file_read,
	              (IOWriteFunction)file_write,
	              (IOStatusFunction)file_status) < 0) {
		return -1;
	}

	// open file blocking
#ifdef _WIN32
	file->handle = open(name, flags, mode);
#else
	file->handle = open(name, flags & ~O_NONBLOCK, mode);
#endif

	if (file->handle == IO_HANDLE_INVALID) {
		return -1;
	}

#ifndef _WIN32
	// enable non-blocking operation
	if ((flags & O_NONBLOCK) != 0) {
		fcntl_flags = fcntl(file->handle, F_GETFL, 0);

		if (fcntl_flags < 0 ||
		    fcntl(file->handle, F_SETFL, fcntl_flags | O_NONBLOCK) < 0) {
			robust_close(file->handle);

			return -1;
		}
	}
#endif

	file->base.read_handle = file->handle;
	file->base.write_handle = file->handle;

	return 0;
}

void file_destroy(File *file) {
	robust_close(file->handle);
}

// sets errno on error
int file_read(File *file, void *buffer, int length) {
	return robust_read(file->handle, buffer, length);
}

// sets errno on error
int file_write(File *file, const void *buffer, int length) {
	return robust_write(file->handle, buffer, length);
}

// sets errno on error
int file_status(File *file, IOStatus *status) {
#ifdef _WIN32
	struct _stat64 stat;
	int rc = _fstat64(file->handle, &stat);
#else
	struct stat stat;
	int rc = fstat(file->handle, &stat);
#endif

	if (rc < 0) {
		return -1;
	}

	status->size = stat.st_size;

	return 0;
}

// sets errno on error
off_t file_seek(File *file, off_t offset, int origin) { // takes lseek origin
	return lseek(file->handle, offset, origin);
}
