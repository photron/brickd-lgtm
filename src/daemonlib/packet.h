/*
 * daemonlib
 * Copyright (C) 2012-2014, 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * packet.h: Packet definition for protocol version 2
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

#ifndef DAEMONLIB_PACKET_H
#define DAEMONLIB_PACKET_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	FUNCTION_DISCONNECT_PROBE = 128,
	FUNCTION_GET_PROTOCOL1_BRICKLET_NAME = 241,
	FUNCTION_GET_CHIP_TEMPERATURE = 242,
	FUNCTION_RESET = 243,
	FUNCTION_WRITE_BRICKLET_PLUGIN = 246,
	FUNCTION_READ_BRICKLET_PLUGIN = 247,
	FUNCTION_WRITE_BRICKLET_UID = 248,
	FUNCTION_READ_BRICKLET_UID = 249,
	FUNCTION_GET_ADC_CALIBRATION = 250,
	FUNCTION_ADC_CALIBRATE = 251,
	FUNCTION_STACK_ENUMERATE = 252,
	CALLBACK_ENUMERATE = 253,
	FUNCTION_ENUMERATE = 254,
	FUNCTION_GET_IDENTITY = 255
} CommonBrickFunctionID;

typedef enum {
	FUNCTION_GET_AUTHENTICATION_NONCE = 1,
	FUNCTION_AUTHENTICATE
} BrickDaemonFunctionID;

typedef enum {
	ENUMERATION_TYPE_AVAILABLE = 0,
	ENUMERATION_TYPE_CONNECTED,
	ENUMERATION_TYPE_DISCONNECTED
} EnumerationType;

typedef enum {
	PACKET_E_SUCCESS = 0,
	PACKET_E_INVALID_PARAMETER,
	PACKET_E_FUNCTION_NOT_SUPPORTED,
	PACKET_E_UNKNOWN_ERROR
} PacketE;

#define PACKET_MAX_DUMP_LENGTH ((int)sizeof(Packet) * 3 + 1)
#define PACKET_MAX_SIGNATURE_LENGTH (64 + PACKET_MAX_DUMP_LENGTH)
#define PACKET_MAX_STACK_ENUMERATE_UIDS 16
#define PACKET_NO_CONNECTED_UID_STR "0\0\0\0\0\0\0\0"
#define PACKET_NO_CONNECTED_UID_STR_LENGTH 8

#include "packed_begin.h"

typedef struct {
	uint32_t uid; // always little endian
	uint8_t length; // packet length including header
	uint8_t function_id;
	uint8_t sequence_number_and_options; // 4 bit sequence number, 1 bit response expected, 3 bit unused
	uint8_t error_code_and_future_use; // 2 bit error code, 6 bit unused
} ATTRIBUTE_PACKED PacketHeader;

typedef struct {
	PacketHeader header;
	uint8_t payload[64];
	union {
		uint8_t optional_data[8];
#ifdef DAEMONLIB_WITH_PACKET_TRACE
		uint64_t trace_id; // zero == invalid, even == request, odd == response
#endif
	};
} ATTRIBUTE_PACKED Packet;

typedef struct {
	PacketHeader header;
	char uid[8];
	char connected_uid[8];
	char position;
	uint8_t hardware_version[3];
	uint8_t firmware_version[3];
	uint16_t device_identifier; // always little endian
	uint8_t enumeration_type;
} ATTRIBUTE_PACKED EnumerateCallback;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED EmptyResponse;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED GetAuthenticationNonceRequest;

typedef struct {
	PacketHeader header;
	uint8_t server_nonce[4];
} ATTRIBUTE_PACKED GetAuthenticationNonceResponse;

typedef struct {
	PacketHeader header;
	uint8_t client_nonce[4];
	uint8_t digest[20];
} ATTRIBUTE_PACKED AuthenticateRequest;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED AuthenticateResponse;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED StackEnumerateRequest;

typedef struct {
	PacketHeader header;
	uint32_t uids[PACKET_MAX_STACK_ENUMERATE_UIDS];
} ATTRIBUTE_PACKED StackEnumerateResponse;

#include "packed_end.h"

bool packet_header_is_valid_request(PacketHeader *header, const char **message);
bool packet_header_is_valid_response(PacketHeader *header, const char **message);

uint8_t packet_header_get_sequence_number(PacketHeader *header);
void packet_header_set_sequence_number(PacketHeader *header, uint8_t sequence_number);

bool packet_header_get_response_expected(PacketHeader *header);
void packet_header_set_response_expected(PacketHeader *header, bool response_expected);

PacketE packet_header_get_error_code(PacketHeader *header);
void packet_header_set_error_code(PacketHeader *header, PacketE error_code);

const char *packet_get_response_type(Packet *packet);

char *packet_get_request_signature(char *signature, Packet *packet);
char *packet_get_response_signature(char *signature, Packet *packet);
char *packet_get_dump(char *dump, Packet *packet, int length);

bool packet_is_matching_response(Packet *packet, PacketHeader *pending_request);

#ifdef DAEMONLIB_WITH_PACKET_TRACE

#define packet_add_trace(packet) packet_add_trace_(packet, __FILE__, __LINE__)

uint64_t packet_get_next_request_trace_id(void);
uint64_t packet_get_next_response_trace_id(void);
void packet_add_trace_(Packet *packet, const char *filename, int line);

#else

#define packet_add_trace(packet) ((void)(packet))

#endif

#endif // DAEMONLIB_PACKET_H
