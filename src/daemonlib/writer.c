/*
 * daemonlib
 * Copyright (C) 2014-2017, 2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * writer.c: Buffered packet writer for I/O devices
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
#include <string.h>

#include "writer.h"

#include "event.h"
#include "log.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define MAX_QUEUED_WRITES 32768

static void writer_handle_write(void *opaque) {
	Writer *writer = opaque;
	PartialPacket *partial_packet;
	void *remaining_data;
	int remaining_length;
	int rc;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	char recipient_signature[WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH];

	if (writer->backlog.count == 0) {
		return;
	}

	// write remaining packet data
	partial_packet = queue_peek(&writer->backlog);
	remaining_data = (uint8_t *)&partial_packet->packet + partial_packet->written;
	remaining_length = (int)partial_packet->packet.header.length - partial_packet->written;

	if (remaining_length > 0) {
		rc = io_write(writer->io, remaining_data, remaining_length);

		if (rc < 0) {
			log_error("Could not send queued %s (%s) to %s, disconnecting %s: %s (%d)",
			          writer->packet_type,
			          writer->packet_signature(packet_signature, &partial_packet->packet),
			          writer->recipient_signature(recipient_signature, false, writer->opaque),
			          writer->recipient_name,
			          get_errno_name(errno), errno);

			writer->recipient_disconnect(writer->opaque);

			return;
		}

		partial_packet->written += rc;
	}

	// if packet was no completely written then don't remove it from the backlog yet
	if (partial_packet->written < (int)partial_packet->packet.header.length) {
		return;
	}

	log_packet_debug("Sent queued %s (%s) to %s, %d %s(s) left in write backlog",
	                 writer->packet_type,
	                 writer->packet_signature(packet_signature, &partial_packet->packet),
	                 writer->recipient_signature(recipient_signature, false, writer->opaque),
	                 writer->backlog.count - 1,
	                 writer->packet_type);

	queue_pop(&writer->backlog, NULL);

	if (writer->backlog.count == 0) {
		// last queued packet handled, deregister for write events
		event_modify_source(writer->io->write_handle, EVENT_SOURCE_TYPE_GENERIC,
		                    EVENT_WRITE, 0, NULL, NULL);
	}
}

static int writer_push_packet_to_backlog(Writer *writer, Packet *packet, int written) {
	PartialPacket *queued_partial_packet;
	char recipient_signature[WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH];
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	uint32_t packets_to_drop;

	log_packet_debug("%s is not ready to receive, pushing %s to write backlog (count: %d + 1)",
	                 writer->recipient_signature(recipient_signature, true, writer->opaque),
	                 writer->packet_type, writer->backlog.count);

	if (writer->backlog.count >= MAX_QUEUED_WRITES) {
		packets_to_drop = writer->backlog.count - MAX_QUEUED_WRITES + 1;

		log_warn("Write backlog for %s is full, dropping %u queued %s(s), %u + %u dropped in total",
		         writer->recipient_signature(recipient_signature, false, writer->opaque),
		         packets_to_drop, writer->packet_type,
		         writer->dropped_packets, packets_to_drop);

		writer->dropped_packets += packets_to_drop;

		while (writer->backlog.count >= MAX_QUEUED_WRITES) {
			queue_pop(&writer->backlog, NULL);
		}
	}

	queued_partial_packet = queue_push(&writer->backlog);

	if (queued_partial_packet == NULL) {
		log_error("Could not push %s (%s) to write backlog for %s, discarding %s: %s (%d)",
		          writer->packet_type,
		          writer->packet_signature(packet_signature, packet),
		          writer->recipient_signature(recipient_signature, false, writer->opaque),
		          writer->packet_type,
		          get_errno_name(errno), errno);

		return -1;
	}

	memcpy(&queued_partial_packet->packet, packet, packet->header.length);
	queued_partial_packet->written = written;

	if (writer->backlog.count == 1) {
		// first queued packet, register for write events
		if (event_modify_source(writer->io->write_handle, EVENT_SOURCE_TYPE_GENERIC,
		                        0, EVENT_WRITE, writer_handle_write, writer) < 0) {
			// FIXME: how to handle this error?
			return -1;
		}
	}

	return 0;
}

int writer_create(Writer *writer, IO *io,
                  const char *packet_type,
                  WriterPacketSignatureFunction packet_signature,
                  const char *recipient_name,
                  WriterRecipientSignatureFunction recipient_signature,
                  WriterRecipientDisconnectFunction recipient_disconnect,
                  void *opaque) {
	writer->io = io;
	writer->packet_type = packet_type;
	writer->packet_signature = packet_signature;
	writer->recipient_name = recipient_name;
	writer->recipient_signature = recipient_signature;
	writer->recipient_disconnect = recipient_disconnect;
	writer->opaque = opaque;
	writer->dropped_packets = 0;

	// create write queue
	if (queue_create(&writer->backlog, sizeof(PartialPacket)) < 0) {
		log_error("Could not create backlog: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void writer_destroy(Writer *writer) {
	char recipient_signature[WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH];

	if (writer->backlog.count > 0) {
		log_warn("Destroying writer for %s while %d %s(s) have not been send",
		         writer->recipient_signature(recipient_signature, false, writer->opaque),
		         writer->backlog.count,
		         writer->packet_type);

		event_modify_source(writer->io->write_handle, EVENT_SOURCE_TYPE_GENERIC,
		                    EVENT_WRITE, 0, NULL, NULL);
	}

	queue_destroy(&writer->backlog, NULL);
}

// returns -1 on error, 0 if the packet was completely written and 1 if the
// packet was completely or partly pushed to the backlog
int writer_write(Writer *writer, Packet *packet) {
	int rc;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	char recipient_signature[WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH];

	// there is already a backlog, push complete packet to backlog
	if (writer->backlog.count > 0) {
		if (writer_push_packet_to_backlog(writer, packet, 0) < 0) {
			return -1;
		}

		return 1;
	}

	// if there is no backlog, try to write
	rc = io_write(writer->io, packet, packet->header.length);

	if (rc < 0) {
		if (errno_would_block()) {
			// if write failed with EWOULDBLOCK, push complete packet to backlog
			if (writer_push_packet_to_backlog(writer, packet, 0) < 0) {
				return -1;
			}

			return 1;
		}

		// otherwise give up and disconnect the recipient
		log_error("Could not send %s (%s) to %s, disconnecting %s: %s (%d)",
		          writer->packet_type,
		          writer->packet_signature(packet_signature, packet),
		          writer->recipient_signature(recipient_signature, false, writer->opaque),
		          writer->recipient_name,
		          get_errno_name(errno), errno);

		writer->recipient_disconnect(writer->opaque);

		return -1;
	} else if (rc < packet->header.length) {
		// packet was not written completely, push remaining packet to backlog
		if (writer_push_packet_to_backlog(writer, packet, rc) < 0) {
			return -1;
		}

		return 1;
	}

	return 0;
}
