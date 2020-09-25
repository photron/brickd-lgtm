/*
 * daemonlib
 * Copyright (C) 2012-2017, 2020 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * socket_winapi.c: WinAPI based socket implementation
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
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h> // for IPV6_V6ONLY
#include <windows.h>

#include "socket.h"

#include "utils.h"

// sets errno on error
static int socket_prepare(Socket *socket) {
	BOOL no_delay = TRUE;
	unsigned long non_blocking = 1;

	// enable no-delay option
	if (socket->family == AF_INET || socket->family == AF_INET6) {
		if (setsockopt(socket->handle, IPPROTO_TCP, TCP_NODELAY,
		               (const char *)&no_delay, sizeof(no_delay)) == SOCKET_ERROR) {
			errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

			return -1;
		}
	}

	// enable non-blocking operation
	if (ioctlsocket(socket->handle, FIONBIO, &non_blocking) == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return 0;
}

void socket_destroy_platform(Socket *socket) {
	// check if socket is actually open, as socket_create deviates from
	// the common pattern of allocation the wrapped resource
	if (socket->handle != IO_HANDLE_INVALID) {
		shutdown(socket->handle, SD_BOTH);
		closesocket(socket->handle);
	}
}

// sets errno on error
int socket_open(Socket *socket_, int family, int type, int protocol) {
	socket_->handle = socket(family, type, protocol);

	if (socket_->handle == IO_HANDLE_INVALID) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	socket_->family = family;
	socket_->base.read_handle = socket_->handle;
	socket_->base.write_handle = socket_->handle;

	return 0;
}

// sets errno on error
int socket_accept_platform(Socket *socket, Socket *accepted_socket,
                           struct sockaddr *address, socklen_t *length) {
	int saved_errno;

	// accept socket
	accepted_socket->handle = accept(socket->handle, address, length);

	if (accepted_socket->handle == IO_HANDLE_INVALID) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	accepted_socket->family = address->sa_family;
	accepted_socket->base.read_handle = accepted_socket->handle;
	accepted_socket->base.write_handle = accepted_socket->handle;

	// prepare socket
	if (socket_prepare(accepted_socket) < 0) {
		saved_errno = errno;

		closesocket(accepted_socket->handle);

		errno = saved_errno;

		return -1;
	}

	return 0;
}

// sets errno on error
int socket_bind(Socket *socket, const struct sockaddr *address, socklen_t length) {
	if (bind(socket->handle, address, length) == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return socket_prepare(socket);
}

// sets errno on error
int socket_listen_platform(Socket *socket, int backlog) {
	if (listen(socket->handle, backlog) == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return 0;
}

// sets errno on error
int socket_connect(Socket *socket, struct sockaddr *address, int length) {
	if (connect(socket->handle, address, length) == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return socket_prepare(socket);
}

// sets errno on error
int socket_receive_platform(Socket *socket, void *buffer, int length) {
	length = recv(socket->handle, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int socket_send_platform(Socket *socket, const void *buffer, int length) {
	length = send(socket->handle, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int socket_set_address_reuse(Socket *socket, bool address_reuse) {
	DWORD on = address_reuse ? TRUE : FALSE;
	int rc = setsockopt(socket->handle, SOL_SOCKET, SO_REUSEADDR,
	                    (const char *)&on, sizeof(on));

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_set_dual_stack(Socket *socket, bool dual_stack) {
	DWORD on = dual_stack ? 0 : 1;
	int rc;

#ifndef DAEMONLIB_UWP_BUILD // UWP implies Windows 10
	if ((DWORD)(LOBYTE(LOWORD(GetVersion()))) < 6) {
		// the IPV6_V6ONLY option is only supported on Vista or later. on
		// Windows XP dual-stack mode is not supported at all. so fail with
		// expected error if dual-stack mode should be enabled and pretend
		// that it got disabled otherwise as this is the case on Windows XP
		// anyway
		if (dual_stack) {
			errno = ERRNO_WINAPI_OFFSET + WSAENOPROTOOPT;

			return -1;
		}

		return 0;
	}
#endif

	rc = setsockopt(socket->handle, IPPROTO_IPV6, IPV6_V6ONLY,
	                (const char *)&on, sizeof(on));

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
struct addrinfo *socket_hostname_to_address(const char *hostname, uint16_t port) {
	char service[32];
	struct addrinfo hints;
	struct addrinfo *resolved = NULL;

	snprintf(service, sizeof(service), "%u", port);

	memset(&hints, 0, sizeof(hints));

	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, service, &hints, &resolved) != 0) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return NULL;
	}

	return resolved;
}

void socket_free_address(struct addrinfo *address) {
	freeaddrinfo(address);
}

// sets errno on error
int socket_address_to_hostname(struct sockaddr *address, socklen_t address_length,
                               char *hostname, int hostname_length,
                               char *port, int port_length) {
	if (getnameinfo(address, address_length,
	                hostname, hostname_length,
	                port, port_length,
	                NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return 0;
}
