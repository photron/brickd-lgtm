/*
 * daemonlib
 * Copyright (C) 2012-2014, 2017, 2019-2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * pipe_winapi.c: WinAPI based pipe implementation
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
 * pipes are used to inject events into the select based event loop. this
 * implementation uses a pair of sockets to create a pipe, because select
 * can only be used with sockets on Windows.
 */

#include <errno.h>
#include <winsock2.h>

#include "pipe.h"

#include "utils.h"

// sets errno on error
// FIXME: maybe use IPv6 if available
int pipe_create(Pipe *pipe, uint32_t flags) {
	IOHandle listener = IO_HANDLE_INVALID;
	struct sockaddr_in address;
	int length = sizeof(address);
	int rc;
	unsigned long flag = 1;

	if (io_create(&pipe->base, "pipe",
	              (IODestroyFunction)pipe_destroy,
	              (IOReadFunction)pipe_read,
	              (IOWriteFunction)pipe_write,
	              NULL) < 0) {
		return -1;
	}

	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listener == IO_HANDLE_INVALID) {
		goto error;
	}

	memset(&address, 0, length);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;

	rc = bind(listener, (const struct sockaddr *)&address, length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	rc = getsockname(listener, (struct sockaddr *)&address, &length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	if (listen(listener, 1) == SOCKET_ERROR) {
		goto error;
	}

	pipe->base.read_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (pipe->base.read_handle == IO_HANDLE_INVALID) {
		goto error;
	}

	rc = connect(pipe->base.read_handle, (const struct sockaddr *)&address, length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	pipe->base.write_handle = accept(listener, NULL, NULL);

	if (pipe->base.write_handle == IO_HANDLE_INVALID) {
		goto error;
	}

	if ((flags & PIPE_FLAG_NON_BLOCKING_READ) != 0 &&
	    ioctlsocket(pipe->base.read_handle, FIONBIO, &flag) == SOCKET_ERROR) {
		goto error;
	}

	if ((flags & PIPE_FLAG_NON_BLOCKING_WRITE) != 0 &&
	    ioctlsocket(pipe->base.write_handle, FIONBIO, &flag) == SOCKET_ERROR) {
		goto error;
	}

	closesocket(listener);

	return 0;

error:
	rc = WSAGetLastError();

	if (listener != IO_HANDLE_INVALID) {
		closesocket(listener);
	}

	if (pipe->base.read_handle != IO_HANDLE_INVALID) {
		closesocket(pipe->base.read_handle);
	}

	if (pipe->base.write_handle != IO_HANDLE_INVALID) {
		closesocket(pipe->base.write_handle);
	}

	errno = ERRNO_WINAPI_OFFSET + rc;

	return -1;
}

void pipe_destroy(Pipe *pipe) {
	closesocket(pipe->base.read_handle);
	closesocket(pipe->base.write_handle);
}

// sets errno on error
int pipe_read(Pipe *pipe, void *buffer, int length) {
	// FIXME: handle interruption
	length = recv(pipe->base.read_handle, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int pipe_write(Pipe *pipe, const void *buffer, int length) {
	// FIXME: handle partial write and interruption
	length = send(pipe->base.write_handle, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}
