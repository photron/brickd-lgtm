/*
 * daemonlib
 * Copyright (C) 2014, 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * writer.h: Buffered packet writer for I/O devices
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

#ifndef DAEMONLIB_WRITER_H
#define DAEMONLIB_WRITER_H

#include <stdbool.h>

#include "io.h"
#include "packet.h"
#include "queue.h"

#define WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH 256

typedef char *(*WriterPacketSignatureFunction)(char *signature, Packet *packet);
typedef char *(*WriterRecipientSignatureFunction)(char *signature, bool upper, void *opaque);
typedef void (*WriterRecipientDisconnectFunction)(void *opaque);

typedef struct {
	Packet packet;
	int written;
} PartialPacket;

typedef struct {
	IO *io;
	const char *packet_type; // for display purpose
	WriterPacketSignatureFunction packet_signature;
	const char *recipient_name; // for display purpose
	WriterRecipientSignatureFunction recipient_signature;
	WriterRecipientDisconnectFunction recipient_disconnect;
	void *opaque;
	uint32_t dropped_packets;
	Queue backlog;
} Writer;

// FIXME: rework this to work for mesh packets as well

int writer_create(Writer *writer, IO *io,
                  const char *packet_type,
                  WriterPacketSignatureFunction packet_signature,
                  const char *recipient_name,
                  WriterRecipientSignatureFunction recipient_signature,
                  WriterRecipientDisconnectFunction recipient_disconnect,
                  void *opaque);
void writer_destroy(Writer *writer);

int writer_write(Writer *writer, Packet *packet);

#endif // DAEMONLIB_WRITER_H
