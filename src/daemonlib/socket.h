/*
 * daemonlib
 * Copyright (C) 2012-2017, 2019-2020 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * socket.h: Socket specific functions
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

#ifndef DAEMONLIB_SOCKET_H
#define DAEMONLIB_SOCKET_H

#include <stdbool.h>
#include <stdint.h>
#ifdef _WIN32
	#include <ws2tcpip.h> // for socklen_t
#else
	#include <netinet/in.h>
#endif

#include "array.h"
#include "io.h"

typedef struct _Socket Socket;

typedef Socket *(*SocketCreateAllocatedFunction)(void);
typedef void (*SocketDestroyFunction)(Socket *socket);
typedef int (*SocketReceiveFunction)(Socket *socket, void *buffer, int length);
typedef int (*SocketSendFunction)(Socket *socket, const void *buffer, int length);

struct _Socket {
	IO base;

	IOHandle handle;
	int family;
	SocketCreateAllocatedFunction create_allocated;
	SocketDestroyFunction destroy;
	SocketReceiveFunction receive;
	SocketSendFunction send;
};

// FIXME: maybe merge socket_create and socket_open
int socket_create(Socket *socket);
Socket *socket_create_allocated(void);
void socket_destroy(Socket *socket);

int socket_open(Socket *socket, int family, int type, int protocol);
Socket *socket_accept(Socket *socket, struct sockaddr *address, socklen_t *length);

int socket_bind(Socket *socket, const struct sockaddr *address, socklen_t length);
int socket_listen(Socket *socket, int backlog,
                  SocketCreateAllocatedFunction create_allocated);

int socket_connect(Socket *socket, struct sockaddr *address, int length);

int socket_receive(Socket *socket, void *buffer, int length);
int socket_send(Socket *socket, const void *buffer, int length);

int socket_set_address_reuse(Socket *socket, bool address_reuse);
int socket_set_dual_stack(Socket *socket, bool dual_stack);

struct addrinfo *socket_hostname_to_address(const char *hostname, uint16_t port);
void socket_free_address(struct addrinfo *address);
int socket_address_to_hostname(struct sockaddr *address, socklen_t address_length,
                               char *hostname, int hostname_length,
                               char *port, int port_length);

// FIXME: add socket_open_client

void socket_open_server(Array *sockets, const char *address, uint16_t port, bool dual_stack,
                        SocketCreateAllocatedFunction create_allocated);

#endif // DAEMONLIB_SOCKET_H
