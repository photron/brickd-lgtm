/*
 * daemonlib
 * Copyright (C) 2014, 2016-2017, 2019-2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * io.c: Base for all I/O devices
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

/*
 * this is the base for all client I/O operations. the purpose of this is
 * to have a common base for the different client I/O sources in brickd.
 * this includes file based I/O to handle the USB gadget interface for the
 * RED Brick, as well as BSD and WinAPI socket based I/O for the normal TCP/IP
 * clients. finally, on top of the plain socket sits the WebSocket for browser
 * clients.
 *
 * all I/O operations have to be don't non-blocking and have to be integrated
 * with the (e)poll/select based event loop.
 */

#include <errno.h>
#include <stdlib.h>

#include "io.h"

int io_create(IO *io, const char *type,
              IODestroyFunction destroy,
              IOReadFunction read,
              IOWriteFunction write,
              IOStatusFunction status) {
	io->read_handle = IO_HANDLE_INVALID;
	io->write_handle = IO_HANDLE_INVALID;
	io->type = type;
	io->destroy = destroy;
	io->read = read;
	io->write = write;
	io->status = status;

	return 0;
}

void io_destroy(IO *io) {
	if (io->destroy != NULL) {
		io->destroy(io);
	}
}

// sets errno on error
int io_read(IO *io, void *buffer, int length) {
	if (io->read == NULL) {
		errno = ENOSYS;

		return -1;
	}

	return io->read(io, buffer, length);
}

// sets errno on error
int io_write(IO *io, const void *buffer, int length) {
	if (io->write == NULL) {
		errno = ENOSYS;

		return -1;
	}

	return io->write(io, buffer, length);
}

// sets errno on error
int io_status(IO *io, IOStatus *status) {
	if (io->status == NULL) {
		errno = ENOSYS;

		return -1;
	}

	return io->status(io, status);
}
