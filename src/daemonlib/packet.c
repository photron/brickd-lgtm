/*
 * daemonlib
 * Copyright (C) 2012-2016, 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * packet.c: Packet definition for protocol version 2
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
 * functions for validating, packing, unpacking and comparing packets.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "packet.h"

#include "base58.h"
#include "log.h"
#include "macros.h"
#include "utils.h"

#ifdef DAEMONLIB_WITH_PACKET_TRACE

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#endif

STATIC_ASSERT(sizeof(PacketHeader) == 8, "PacketHeader has invalid size");
STATIC_ASSERT(sizeof(Packet) == 80, "Packet has invalid size");
STATIC_ASSERT(sizeof(EnumerateCallback) == 34, "EnumerateCallback has invalid size");
STATIC_ASSERT(sizeof(GetAuthenticationNonceRequest) == 8, "GetAuthenticationNonceRequest has invalid size");
STATIC_ASSERT(sizeof(GetAuthenticationNonceResponse) == 12, "GetAuthenticationNonceResponse has invalid size");
STATIC_ASSERT(sizeof(AuthenticateRequest) == 32, "AuthenticateRequest has invalid size");
STATIC_ASSERT(sizeof(StackEnumerateRequest) == 8, "StackEnumerateRequest has invalid size");
STATIC_ASSERT(sizeof(StackEnumerateResponse) == 72, "StackEnumerateResponse has invalid size");

#ifdef DAEMONLIB_WITH_PACKET_TRACE

#define TRACE_BUFFER_SIZE 1000

typedef struct {
	uint64_t trace_id;
	uint64_t timestamp; // microseconds
	PacketHeader header;
	const char *filename; // __FILE__
	int line; // __LINE__
} PacketTrace;

static uint64_t _next_request_trace_id = 2; // start even
static uint64_t _next_response_trace_id = UINT64_MAX; // start odd and high
static PacketTrace _trace_buffer[TRACE_BUFFER_SIZE];
static int _trace_buffer_used = 0;

#endif

bool packet_header_is_valid_request(PacketHeader *header, const char **message) {
	if (header->length < (int)sizeof(PacketHeader)) {
		if (message != NULL) {
			*message = "Length is too small";
		}

		return false;
	}

	if (header->length > (int)sizeof(Packet)) {
		if (message != NULL) {
			*message = "Length is too big";
		}

		return false;
	}

	if (header->function_id == 0) {
		if (message != NULL) {
			*message = "Invalid function ID";
		}

		return false;
	}

	if (packet_header_get_sequence_number(header) == 0) {
		if (message != NULL) {
			*message = "Invalid sequence number";
		}

		return false;
	}

	return true;
}

bool packet_header_is_valid_response(PacketHeader *header, const char **message) {
	if (header->length < (int)sizeof(PacketHeader)) {
		if (message != NULL) {
			*message = "Length is too small";
		}

		return false;
	}

	if (header->length > (int)sizeof(Packet)) {
		if (message != NULL) {
			*message = "Length is too big";
		}

		return false;
	}

	if (uint32_from_le(header->uid) == 0) {
		if (message != NULL) {
			*message = "Invalid UID";
		}

		return false;
	}

	if (header->function_id == 0) {
		if (message != NULL) {
			*message = "Invalid function ID";
		}

		return false;
	}

	if (!packet_header_get_response_expected(header)) {
		if (message != NULL) {
			*message = "Invalid response expected bit";
		}

		return false;
	}

	return true;
}

uint8_t packet_header_get_sequence_number(PacketHeader *header) {
	return (header->sequence_number_and_options >> 4) & 0x0F;
}

void packet_header_set_sequence_number(PacketHeader *header, uint8_t sequence_number) {
	header->sequence_number_and_options &= ~0xF0;
	header->sequence_number_and_options |= (sequence_number << 4) & 0xF0;
}

bool packet_header_get_response_expected(PacketHeader *header) {
	return ((header->sequence_number_and_options >> 3) & 0x01) == 0x01;
}

void packet_header_set_response_expected(PacketHeader *header, bool response_expected) {
	if (response_expected) {
		header->sequence_number_and_options |= 0x01 << 3;
	} else {
		header->sequence_number_and_options &= ~(0x01 << 3);
	}
}

PacketE packet_header_get_error_code(PacketHeader *header) {
	return (header->error_code_and_future_use >> 6) & 0x03;
}

void packet_header_set_error_code(PacketHeader *header, PacketE error_code) {
	header->error_code_and_future_use &= ~0xC0;
	header->error_code_and_future_use |= (error_code << 6) & 0xC0;
}

const char *packet_get_response_type(Packet *packet) {
	if (packet_header_get_sequence_number(&packet->header) != 0) {
		return "response";
	}

	if (packet->header.function_id != CALLBACK_ENUMERATE) {
		return "callback";
	}

	switch (((EnumerateCallback *)packet)->enumeration_type) {
	case ENUMERATION_TYPE_AVAILABLE:
		return "enumerate-available callback";

	case ENUMERATION_TYPE_CONNECTED:
		return "enumerate-connected callback";

	case ENUMERATION_TYPE_DISCONNECTED:
		return "enumerate-disconnected callback";

	default:
		return "enumerate-<unknown> callback";
	}
}

char *packet_get_request_signature(char *signature, Packet *packet) {
	char base58[BASE58_MAX_LENGTH];
#ifdef DAEMONLIB_WITH_PACKET_TRACE
	uint64_t trace_id = packet->trace_id;
#else
	uint64_t trace_id = 0;
#endif
	char dump[PACKET_MAX_DUMP_LENGTH];

	snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH,
	         "U: %s, L: %u, F: %u, S: %u, R: %d, I: %" PRIu64 ", packet: %s",
	         base58_encode(base58, uint32_from_le(packet->header.uid)),
	         packet->header.length,
	         packet->header.function_id,
	         packet_header_get_sequence_number(&packet->header),
	         packet_header_get_response_expected(&packet->header) ? 1 : 0,
	         trace_id,
	         packet_get_dump(dump, packet, packet->header.length));

	return signature;
}

char *packet_get_response_signature(char *signature, Packet *packet) {
	char base58[BASE58_MAX_LENGTH];
#ifdef DAEMONLIB_WITH_PACKET_TRACE
	uint64_t trace_id = packet->trace_id;
#else
	uint64_t trace_id = 0;
#endif
	char dump[PACKET_MAX_DUMP_LENGTH];

	if (packet_header_get_sequence_number(&packet->header) != 0) {
		snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH,
		         "U: %s, L: %u, F: %u, S: %u, E: %d, I: %" PRIu64 ", packet: %s",
		         base58_encode(base58, uint32_from_le(packet->header.uid)),
		         packet->header.length,
		         packet->header.function_id,
		         packet_header_get_sequence_number(&packet->header),
		         (int)packet_header_get_error_code(&packet->header),
		         trace_id,
		         packet_get_dump(dump, packet, packet->header.length));
	} else {
		snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH,
		         "U: %s, L: %u, F: %u, I: %" PRIu64 ", packet: %s",
		         base58_encode(base58, uint32_from_le(packet->header.uid)),
		         packet->header.length,
		         packet->header.function_id,
		         trace_id,
		         packet_get_dump(dump, packet, packet->header.length));
	}

	return signature;
}

char *packet_get_dump(char *dump, Packet *packet, int length) {
	int i;

	if (length > (int)sizeof(Packet)) {
		length = (int)sizeof(Packet);
	}

	for (i = 0; i < length; ++i) {
		snprintf(dump + i * 3, 4, "%02X ", ((uint8_t *)packet)[i]);
	}

	if (length > 0) {
		dump[length * 3 - 1] = '\0';
	} else {
		dump[0] = '\0';
	}

	return dump;
}

bool packet_is_matching_response(Packet *packet, PacketHeader *pending_request) {
	if (packet->header.uid != pending_request->uid) {
		return false;
	}

	if (packet->header.function_id != pending_request->function_id) {
		return false;
	}

	if (packet_header_get_sequence_number(&packet->header) !=
	    packet_header_get_sequence_number(pending_request)) {
		return false;
	}

	return true;
}

#ifdef DAEMONLIB_WITH_PACKET_TRACE

uint64_t packet_get_next_request_trace_id(void) {
	return __sync_fetch_and_add(&_next_request_trace_id, 2); // keep even
}

uint64_t packet_get_next_response_trace_id(void) {
	return __sync_fetch_and_sub(&_next_response_trace_id, 2); // keep even
}

void packet_add_trace_(Packet *packet, const char *filename, int line) {
	PacketTrace *trace = &_trace_buffer[_trace_buffer_used++];
	FILE *fp;
	int i;

	trace->trace_id = packet->trace_id;
	trace->timestamp = microtime();
	trace->header = packet->header;
	trace->filename = filename;
	trace->line = line;

	if (_trace_buffer_used == TRACE_BUFFER_SIZE) {
		log_info("Writing packet trace to /tmp/daemonlib-packet-trace");

		fp = fopen("/tmp/daemonlib-packet-trace", "wb");

		if (fp == NULL) {
			_trace_buffer_used = 0;

			log_error("Could not open /tmp/daemonlib-packet-trace");

			return;
		}

		for (i = 0; i < _trace_buffer_used; ++i) {
			trace = &_trace_buffer[i];

			fwrite(&trace->trace_id, 1, sizeof(trace->trace_id), fp);
			fwrite(&trace->timestamp, 1, sizeof(trace->timestamp), fp);
			fwrite(&trace->header, 1, sizeof(trace->header), fp);
			fwrite(trace->filename, 1, strlen(trace->filename) + 1, fp);
			fwrite(&trace->line, 1, sizeof(trace->line), fp);
		}

		fflush(fp);
		fclose(fp);

		_trace_buffer_used = 0;
	}
}

#endif
