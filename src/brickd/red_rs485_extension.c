/*
 * brickd
 * Copyright (C) 2014, 2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * red_rs485_extension.c: RS485 extension support for RED Brick
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
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <termios.h>

#include <daemonlib/base58.h>
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/packet.h>
#include <daemonlib/pipe.h>
#include <daemonlib/gpio_red.h>
#include <daemonlib/red_i2c_eeprom.h>
#include <daemonlib/threads.h>
#include <daemonlib/timer.h>
#include <daemonlib/conf_file.h>

#include "red_rs485_extension.h"

#include "hardware.h"
#include "network.h"
#include "stack.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define CRC_ERROR_COUNT_UPDATE_INTERVAL 4000000 // 4 seconds in microseconds
#define RS485_EXTENSION_CRC_ERROR_COUNT_COMMENT   "# This file is written by brickd's RS485 stack."
#define RS485_EXTENSION_CRC_ERROR_COUNT_FILE_PATH "/tmp/extension_rs485_crc_error_count.conf"

static ConfFile crc_error_count_file;
static uint64_t crc_error_count_value = 0;
static Timer crc_error_count_update_timer;

#define RS485_EXTENSION_FUNCTION_CODE                                   100 // Custom modbus function code

// Serial interface config stuffs
#define RECEIVE_BUFFER_SIZE                                             1024

#if BRICKD_WITH_RED_BRICK == 9
	#define RS485_EXTENSION_SERIAL_DEVICE                               "/dev/ttyS0"
#else
	#define RS485_EXTENSION_SERIAL_DEVICE                               "/dev/ttyS3"
#endif

// Time related constants
static uint64_t TIMEOUT = 0;
// delay between polls in nanoseconds. configurable with brickd.conf option poll_delay.rs485 in microseconds
static uint64_t MASTER_POLL_SLAVE_INTERVAL = 40000000;
static uint32_t TIMEOUT_BYTES = 86;
static uint64_t last_timer_enable_at_us = 0;
static uint64_t time_passed_from_last_timer_enable = 0;

// Frame related constants
#define RS485_FRAME_HEADER_LENGTH      3
#define RS485_FRAME_FOOTER_LENGTH      2
#define RS485_FRAME_TRIES_DATA         10
#define RS485_FRAME_TRIES_EMPTY        1
#define RS485_FRAME_OVERHEAD           RS485_FRAME_HEADER_LENGTH + RS485_FRAME_FOOTER_LENGTH
#define RS485_FRAME_MAX_CONTENT_DUMP_LENGTH (((int)sizeof(Packet) + RS485_FRAME_OVERHEAD) * 3 + 1)

// Table of CRC values for high-order byte
static const uint8_t table_crc_hi[] = {
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
	0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
	0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
	0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
	0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
	0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
	0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
	0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40
};

// Table of CRC values for low-order byte
static const uint8_t table_crc_lo[] = {
	0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06,
	0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD,
	0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
	0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A,
	0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4,
	0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
	0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3,
	0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
	0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
	0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29,
	0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED,
	0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
	0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60,
	0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67,
	0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
	0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
	0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E,
	0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
	0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71,
	0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92,
	0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
	0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B,
	0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B,
	0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
	0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42,
	0x43, 0x83, 0x41, 0x81, 0x80, 0x40
};

// Data structure definitions
typedef struct {
	Packet packet;
	uint8_t tries_left;
} RS485ExtensionPacket;

typedef struct {
	uint8_t address;
	uint8_t sequence;
	Queue packet_queue;
} RS485Slave;

typedef struct {
	Stack base;
	RS485Slave slaves[EXTENSION_RS485_SLAVES_MAX];
	int slave_num;

	uint32_t baudrate;
	uint8_t parity;
	uint8_t stopbits;
	uint32_t address;
} RS485Extension;

static RS485Extension _red_rs485_extension;
static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH] = {0};
static int _red_rs485_serial_fd = -1; // Serial interface file descriptor

// Variables tracking current states
static char current_request_as_byte_array[sizeof(Packet) + RS485_FRAME_OVERHEAD] = {0};
static int master_current_slave_to_process = -1; // Only used used by master

// Receive buffer
#include <daemonlib/packed_begin.h>

static union {
	struct {
		struct {
			uint8_t address;
			uint8_t function_code;
			uint8_t sequence_number;
		} ATTRIBUTE_PACKED frame;
		Packet packet;
	} ATTRIBUTE_PACKED;
	uint8_t buffer[RECEIVE_BUFFER_SIZE];
} ATTRIBUTE_PACKED _receive;

#include <daemonlib/packed_end.h>

static int _receive_buffer_used = 0;

// Events
static int _master_timer_event = 0;

// Timers
static struct itimerspec master_timer;

// Used as boolean
static bool _initialized = false;
static uint8_t sent_ack_of_data_packet = 0;
static uint8_t send_verify_flag = 0;
static bool master_poll_interval = false;

// RX GPIO pin definitions
static GPIOREDPin _rx_pin; // Active low

// Function prototypes
uint16_t crc16(uint8_t*, uint16_t);
int serial_interface_init(const char*);
void verify_buffer(void);
void send_packet(void);
void init_rxe_pin_state(int);
void serial_data_available_handler(void*);
void master_poll_slave(void);
void master_timeout_handler(void*);
int red_rs485_extension_dispatch_to_rs485(Stack*, Packet*, Recipient*);
void disable_master_timer(void);
void pop_packet_from_slave_queue(void);
bool is_current_request_empty(void);
void seq_pop_poll(void);
void arm_master_poll_slave_interval_timer(void);
bool init_crc_error_count_to_fs(void);
static void update_crc_error_count_to_fs(void *opaque);

// CRC16 function
uint16_t crc16(uint8_t *buffer, uint16_t buffer_length) {
	uint8_t crc_hi = 0xFF; // High CRC byte initialized
	uint8_t crc_lo = 0xFF; // Low CRC byte initialized
	int i;

	// Pass through message buffer
	while (buffer_length--) {
		i = crc_hi ^ *buffer++; // Calculate the CRC
		crc_hi = crc_lo ^ table_crc_hi[i];
		crc_lo = table_crc_lo[i];
	}

	return (crc_hi << 8 | crc_lo);
}

// Function for initializing the serial interface
int serial_interface_init(const char *serial_interface) {
	// Device file opening flags
	int flags = O_RDWR | O_NOCTTY | O_NDELAY | O_EXCL | ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;

	// Opening device file
	if ((_red_rs485_serial_fd = open(serial_interface, flags)) < 0) {
		log_error("Serial device open failed");

		return -1;
	}

	// Serial interface setup

	// Serial interface config struct
	struct termios serial_interface_config;
	struct serial_struct serial_config;
	tcgetattr(_red_rs485_serial_fd, &(serial_interface_config));
	memset(&serial_interface_config, 0, sizeof(serial_interface_config));
	memset(&serial_config, 0, sizeof(serial_config));

	// Control options
	serial_interface_config.c_cflag |= (CREAD | CLOCAL);
	serial_interface_config.c_cflag &= ~CSIZE;
	serial_interface_config.c_cflag |= CS8; // Setting data bits

	if (_red_rs485_extension.stopbits == 1) {
		serial_interface_config.c_cflag &= ~CSTOPB;  // Setting one stop bits
	} else if (_red_rs485_extension.stopbits == 2) {
		serial_interface_config.c_cflag |= CSTOPB; // Setting two stop bits
	} else {
		robust_close(_red_rs485_serial_fd);

		log_error("Error in serial stop bits config");

		return -1;
	}

	if (_red_rs485_extension.parity == EXTENSION_RS485_PARITY_NONE) {
		serial_interface_config.c_cflag &= ~PARENB;  // parity disabled
	} else if (_red_rs485_extension.parity == EXTENSION_RS485_PARITY_EVEN) {
		/* Even */
		serial_interface_config.c_cflag |= PARENB;
		serial_interface_config.c_cflag &= ~PARODD;
	} else if (_red_rs485_extension.parity == EXTENSION_RS485_PARITY_ODD) {
		/* Odd */
		serial_interface_config.c_cflag |= PARENB;
		serial_interface_config.c_cflag |= PARODD;
	} else {
		robust_close(_red_rs485_serial_fd);

		log_error("Error in serial parity config");

		return -1;
	}

	// Setting the baudrate
	serial_config.reserved_char[0] = 0;

	if (ioctl(_red_rs485_serial_fd, TIOCGSERIAL, &serial_config) < 0) {
		log_error("Error setting RS485 serial baudrate");

		return -1;
	}

	serial_config.flags &= ~ASYNC_SPD_MASK;
	serial_config.flags |= ASYNC_SPD_CUST;
	serial_config.custom_divisor = (serial_config.baud_base + (_red_rs485_extension.baudrate / 2)) /
	                               _red_rs485_extension.baudrate;

	if (serial_config.custom_divisor < 1) {
		serial_config.custom_divisor = 1;
	}

	if (ioctl(_red_rs485_serial_fd, TIOCSSERIAL, &serial_config) < 0) {
		log_error("Error setting serial baudrate");

		return -1;
	}

	log_info("Baudrate configured = %d, Effective baudrate = %f",
	         _red_rs485_extension.baudrate,
	         (float)serial_config.baud_base / serial_config.custom_divisor);

	cfsetispeed(&serial_interface_config, B38400);
	cfsetospeed(&serial_interface_config, B38400);

	// Line options
	serial_interface_config.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw input

	// Input options
	if (_red_rs485_extension.parity == EXTENSION_RS485_PARITY_NONE) {
		serial_interface_config.c_iflag &= ~INPCK; // Input check disabled
	} else {
		serial_interface_config.c_iflag |= INPCK; // Input check enabled
	}

	serial_interface_config.c_iflag &= ~(IXON | IXOFF | IXANY); // Software iflow control is disabled

	// Output options
	serial_interface_config.c_oflag &= ~OPOST;

	// Control character options
	serial_interface_config.c_cc[VMIN] = 0;
	serial_interface_config.c_cc[VTIME] = 0;

	tcsetattr(_red_rs485_serial_fd, TCSANOW, &serial_interface_config);

	// Flushing the buffer
	tcflush(_red_rs485_serial_fd, TCIOFLUSH);

	log_info("Serial interface initialized");

	return 0;
}

char *frame_get_content_dump(char *content_dump, uint8_t *frame, int length) {
	int i;

	if (length > (int)sizeof(Packet) + RS485_FRAME_OVERHEAD) {
		length = (int)sizeof(Packet) + RS485_FRAME_OVERHEAD;
	}

	for (i = 0; i < length; ++i) {
		snprintf(content_dump + i * 3, 4, "%02X ", frame[i]);
	}

	if (length > 0) {
		content_dump[length * 3 - 1] = '\0';
	} else {
		content_dump[0] = '\0';
	}

	return content_dump;
}

// Verify packet
void verify_buffer(void) {
	int frame_length;
	uint16_t crc16_calculated;
	uint16_t crc16_on_packet;
	RS485ExtensionPacket* queue_packet;
	int i;
	char frame_content_dump[RS485_FRAME_MAX_CONTENT_DUMP_LENGTH];
	char base58[BASE58_MAX_LENGTH];

	// Check if length byte is available
	if (_receive_buffer_used < 8) {
		return;
	}

	// Calculate packet end index
	frame_length = RS485_FRAME_HEADER_LENGTH + _receive.packet.header.length + RS485_FRAME_FOOTER_LENGTH;

	// Check if complete packet is available
	if (_receive_buffer_used < frame_length) {
		return;
	}

	// If send verify flag was set
	if (send_verify_flag) {
		for (i = 0; i < frame_length; i++) {
			if (_receive.buffer[i] != current_request_as_byte_array[i]) {
				// Move on to next slave
				disable_master_timer();
				log_error("Send verification failed (offset: %d, actual: %u != expected: %u)",
				          i, _receive.buffer[i], current_request_as_byte_array[i]);
				seq_pop_poll();

				return;
			}
		}

		// Send verify successful. Reset flag
		send_verify_flag = 0;
		log_packet_debug("Send verification done");

		if (sent_ack_of_data_packet > 0) {
			// Request processing done. Move on to next slave
			disable_master_timer();

			if (sent_ack_of_data_packet == 1) {
				log_packet_debug("Processed current request");
				++_red_rs485_extension.slaves[master_current_slave_to_process].sequence;
				queue_pop(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue, NULL);
			}

			// Poll next slave after the configured timeout
			arm_master_poll_slave_interval_timer();

			return;
		} else if (_receive_buffer_used == frame_length) {
			// Everything OK. Wait for response now
			log_packet_debug("No more Data. Waiting for response");
			_receive_buffer_used = 0;
			memset(_receive.buffer, 0, RECEIVE_BUFFER_SIZE);

			return;
		} else if (_receive_buffer_used > frame_length) {
			// More data in the receive buffer
			log_packet_debug("Potential partial data in the buffer. Verifying");

			memmove(_receive.buffer, _receive.buffer + frame_length,
			        _receive_buffer_used - frame_length);

			_receive_buffer_used -= frame_length;

			// A recursive call to handle the remaining bytes in the buffer
			if (_receive_buffer_used >= 8) {
				verify_buffer(); // FIXME: turn this into an external loop
			}

			return;
		} else {
			// Undefined state
			disable_master_timer();
			log_error("Undefined receive buffer state");
			seq_pop_poll();

			return;
		}
	}

	// Checking the CRC16 checksum
	crc16_calculated = crc16(_receive.buffer, frame_length - RS485_FRAME_FOOTER_LENGTH);
	crc16_on_packet = (_receive.buffer[frame_length - 2] << 8) | _receive.buffer[frame_length - 1];

	if (crc16_calculated != crc16_on_packet) {
		// Increase CRC error count
		crc_error_count_value++;

		// Move on to next slave
		disable_master_timer();
		log_error("Received response (frame: %s) with CRC-16 mismatch (actual: %04X != expected: %04X)",
		          frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
		          crc16_calculated, crc16_on_packet);
		seq_pop_poll();

		return;
	}

	// Checking address
	if (_receive.frame.address != current_request_as_byte_array[0]) {
		// Move on to next slave
		disable_master_timer();
		log_error("Received response (frame: %s) with address mismatch (actual: %u != expected: %u)",
		          frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
		          _receive.frame.address, current_request_as_byte_array[0]);
		seq_pop_poll();

		return;
	}

	// Checking function code
	if (_receive.frame.function_code != current_request_as_byte_array[1]) {
		// Move on to next slave
		disable_master_timer();
		log_error("Received response (frame: %s) with function code mismatch (actual: %u != expected: %u)",
		          frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
		          _receive.frame.function_code, current_request_as_byte_array[1]);
		seq_pop_poll();

		return;
	}

	// Received empty packet from the other side (UID=0, FID=0)
	if (_receive.packet.header.uid == 0 && _receive.packet.header.function_id == 0) {
		// Checking current sequence number
		if (_receive.frame.sequence_number != current_request_as_byte_array[2]) {
			// Move on to next slave
			disable_master_timer();
			log_error("Received empty response (frame: %s) with sequence number mismatch (actual: %u != expected: %u)",
			          frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
			          _receive.frame.sequence_number, current_request_as_byte_array[2]);
			seq_pop_poll();

			return;
		}

		disable_master_timer();

		log_packet_debug("Received empty response");

		// Updating sequence number
		++_red_rs485_extension.slaves[master_current_slave_to_process].sequence;

		// Popping slave's packet queue
		queue_pop(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue, NULL);

		// Poll next slave after the configured timeout
		arm_master_poll_slave_interval_timer();
	}
	// Received data packet from the other side
	else if (_receive.packet.header.uid != 0 && _receive.packet.header.function_id != 0) {
		// Checking current sequence number
		if (_receive.frame.sequence_number != current_request_as_byte_array[2]) {
			log_warn("Received data response (frame: %s) with sequence number mismatch (actual: %u != expected: %u)",
			         frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
			         _receive.frame.sequence_number, current_request_as_byte_array[2]);
		} else {
			log_packet_debug("Received data response");

			stack_add_recipient(&_red_rs485_extension.base, _receive.packet.header.uid, _receive.frame.address); // FIXME: check return value

			// Send message into brickd dispatcher
			network_dispatch_response(&_receive.packet);
		}

		queue_packet = queue_peek(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue);

		if (queue_packet == NULL) {
			log_warn("Sending ACK for unexpected data response");

			queue_packet = queue_push(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue);

			if (queue_packet == NULL) {
				log_error("Could not push empty request to packet queue for slave %d: %s (%d)",
				          _red_rs485_extension.slaves[master_current_slave_to_process].address,
				          get_errno_name(errno), errno);

				return; // FIXME
			}

			sent_ack_of_data_packet = 2;
		} else {
			sent_ack_of_data_packet = 1;
		}

		// Replace head of slave queue with an ACK
		memset(queue_packet, 0, sizeof(RS485ExtensionPacket));
		queue_packet->tries_left = RS485_FRAME_TRIES_EMPTY;
		queue_packet->packet.header.length = 8;

		_receive_buffer_used = 0;
		memset(_receive.buffer, 0, RECEIVE_BUFFER_SIZE);

		log_packet_debug("Sending ACK of the data response");

		send_packet();
	} else {
		// Undefined packet
		disable_master_timer();
		log_error("Undefined response (frame: %s, U: %s, L: %u, F: %u)",
		          frame_get_content_dump(frame_content_dump, _receive.buffer, frame_length),
		          base58_encode(base58, uint32_from_le(_receive.packet.header.uid)),
		          _receive.packet.header.length,
		          _receive.packet.header.function_id);
		seq_pop_poll();
	}
}

// Send packet
void send_packet(void) {
	uint16_t packet_crc16 = 0;
	uint8_t crc16_first_byte_index = 0;
	RS485Slave* current_slave = NULL;
	RS485ExtensionPacket* packet_to_send = NULL;

	current_slave = &_red_rs485_extension.slaves[master_current_slave_to_process];
	packet_to_send = queue_peek(&current_slave->packet_queue);

	if (packet_to_send == NULL) {
		// Slave's packet queue is empty. Move on to next slave
		log_packet_debug("Slave packet queue empty. Moving on");
		// Poll next slave after the configured timeout
		arm_master_poll_slave_interval_timer();
		return;
	}

	uint8_t rs485_packet[packet_to_send->packet.header.length + RS485_FRAME_OVERHEAD];

	// Assemble packet header
	rs485_packet[0] = current_slave->address;
	rs485_packet[1] = RS485_EXTENSION_FUNCTION_CODE;
	rs485_packet[2] = current_slave->sequence;

	// Assemble Tinkerforge packet
	memcpy(&rs485_packet[3], packet_to_send, packet_to_send->packet.header.length);

	// Calculating CRC16
	packet_crc16 = crc16(rs485_packet, packet_to_send->packet.header.length + RS485_FRAME_HEADER_LENGTH);

	// Assemble the calculated CRC16
	crc16_first_byte_index = packet_to_send->packet.header.length +
	                         RS485_FRAME_HEADER_LENGTH;

	rs485_packet[crc16_first_byte_index] = packet_crc16 >> 8;
	rs485_packet[++crc16_first_byte_index] = packet_crc16 & 0x00FF;

	// Sending packet
	if (robust_write(_red_rs485_serial_fd, &rs485_packet, sizeof(rs485_packet)) <= 0) {
		log_error("Error sending packet on interface, %s (%d)",
		          get_errno_name(errno), errno);

		// Poll next slave after the configured timeout
		arm_master_poll_slave_interval_timer();

		return;
	}

	// Save the packet as byte array
	memcpy(&current_request_as_byte_array, &rs485_packet, sizeof(rs485_packet));

	// Set send verify flag
	send_verify_flag = 1;

	log_packet_debug("Sent packet");

	// Start the master timer
	master_timer.it_interval.tv_sec = 0;
	master_timer.it_interval.tv_nsec = 0;
	master_timer.it_value.tv_sec = 0;
	master_timer.it_value.tv_nsec = TIMEOUT;
	timerfd_settime(_master_timer_event, 0, &master_timer, NULL);
	last_timer_enable_at_us = microtime();
}

// Initialize RX state
void init_rxe_pin_state(int extension) {
	switch (extension) {
	case 0:
		_rx_pin.port_index = GPIO_RED_PORT_B;
		_rx_pin.pin_index = GPIO_RED_PIN_13;
		break;

	case 1:
		_rx_pin.port_index = GPIO_RED_PORT_G;
#if BRICKD_WITH_RED_BRICK == 9
		_rx_pin.pin_index = GPIO_RED_PIN_2;
#else
		_rx_pin.pin_index = GPIO_RED_PIN_5;
#endif
		break;
	}

	gpio_red_mux_configure(_rx_pin, GPIO_RED_MUX_OUTPUT);
	gpio_red_output_clear(_rx_pin);
	log_info("Initialized RS485 RXE state");
}

void disable_master_timer(void) {
	uint64_t dummy_read_buffer = 0;
	if (robust_read(_master_timer_event, &dummy_read_buffer, sizeof(uint64_t)) < 0) {}
	master_timer.it_interval.tv_sec = 0;
	master_timer.it_interval.tv_nsec = 0;
	master_timer.it_value.tv_sec = 0;
	master_timer.it_value.tv_nsec = 0;
	timerfd_settime(_master_timer_event, 0, &master_timer, NULL);
	log_debug("Disabled master timer");
}

// New data available event handler
void serial_data_available_handler(void* opaque) {
	(void)opaque;

	// Check if there is space in the receive buffer
	if (_receive_buffer_used >= RECEIVE_BUFFER_SIZE) {
		log_warn("No more space in the receive buffer. Aborting current request");

		// Poll next slave after the configured timeout
		arm_master_poll_slave_interval_timer();

		return;
	}

	// Put newly received bytes on the specific index in receive buffer
	int bytes_received = robust_read(_red_rs485_serial_fd,
	                                 _receive.buffer + _receive_buffer_used,
	                                 RECEIVE_BUFFER_SIZE - _receive_buffer_used);

	if (bytes_received < 0) {
		// FIXME: log error?
		return;
	}

	_receive_buffer_used += bytes_received;
	verify_buffer();
}

// Master polling slave event handler
void master_poll_slave(void) {
	RS485ExtensionPacket* slave_queue_packet;
	sent_ack_of_data_packet = 0;
	_receive_buffer_used = 0;
	memset(_receive.buffer, 0, RECEIVE_BUFFER_SIZE);

	// Updating current slave to process
	if (++master_current_slave_to_process >= _red_rs485_extension.slave_num) {
		master_current_slave_to_process = 0;
	}

	log_debug("Updated current RS485 slave's index");

	if (_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue.count == 0) {
		// Nothing to send in the slave's queue. So send a poll packet
		slave_queue_packet = queue_push(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue);

		if (slave_queue_packet == NULL) {
			log_error("Could not push empty request to packet queue for slave %d: %s (%d)",
			          _red_rs485_extension.slaves[master_current_slave_to_process].address,
			          get_errno_name(errno), errno);

			return;
		}

		slave_queue_packet->tries_left = RS485_FRAME_TRIES_EMPTY;
		slave_queue_packet->packet.header.length = 8;

		log_packet_debug("Sending empty packet to slave ID = %d, Sequence number = %d",
		                 _red_rs485_extension.slaves[master_current_slave_to_process].address,
		                 _red_rs485_extension.slaves[master_current_slave_to_process].sequence);

		// The timer will be fired by the send function
		send_packet();
	} else {
		log_packet_debug("Sending packet from queue to slave ID = %d, Sequence number = %d",
		                 _red_rs485_extension.slaves[master_current_slave_to_process].address,
		                 _red_rs485_extension.slaves[master_current_slave_to_process].sequence);

		// Slave's packet queue if not empty. Send the packet that is at the head of the queue

		// The timer will be fired by the send function
		send_packet();
	}
}

// Master timer event handler
void master_timeout_handler(void* opaque) {
	(void)opaque;

	disable_master_timer();

	if (master_poll_interval) {
		// For some unknown reason the timer randomly times out or this timeout function is called
		// much long before the actual timeout. This is a fix to this problem
		// until we find the real problem
		time_passed_from_last_timer_enable = (microtime() - last_timer_enable_at_us) * 1000;

		if (time_passed_from_last_timer_enable < MASTER_POLL_SLAVE_INTERVAL) {
			master_timer.it_interval.tv_sec = 0;
			master_timer.it_interval.tv_nsec = 0;
			master_timer.it_value.tv_sec = 0;
			master_timer.it_value.tv_nsec = MASTER_POLL_SLAVE_INTERVAL;
			timerfd_settime(_master_timer_event, 0, &master_timer, NULL);
			last_timer_enable_at_us = microtime();

			return;
		}

		log_debug("Master poll slave interval timed out... time to poll next slave");
		master_poll_interval = false;
		master_poll_slave();

		return;
	}

	// For some unknown reason the timer randomly times out or this timeout function is called
	// much long before the actual timeout. This is a fix to this problem
	// until we find the real problem
	time_passed_from_last_timer_enable = (microtime() - last_timer_enable_at_us) * 1000;

	if (time_passed_from_last_timer_enable < TIMEOUT) {
		master_timer.it_interval.tv_sec = 0;
		master_timer.it_interval.tv_nsec = 0;
		master_timer.it_value.tv_sec = 0;
		master_timer.it_value.tv_nsec = TIMEOUT;
		timerfd_settime(_master_timer_event, 0, &master_timer, NULL);
		last_timer_enable_at_us = microtime();

		return;
	}

	log_debug("Current request timed out. Moving on");

	// Current request timedout. Move on to next slave
	if (is_current_request_empty()) {
		++_red_rs485_extension.slaves[master_current_slave_to_process].sequence;
	}

	pop_packet_from_slave_queue();

	// Poll next slave after the configured timeout
	arm_master_poll_slave_interval_timer();
}

void pop_packet_from_slave_queue(void) {
	RS485ExtensionPacket* current_slave_queue_packet;
	current_slave_queue_packet = queue_peek(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue);

	if (current_slave_queue_packet != NULL && --current_slave_queue_packet->tries_left == 0) {
		queue_pop(&_red_rs485_extension.slaves[master_current_slave_to_process].packet_queue, NULL);
	}
}

bool is_current_request_empty(void) {
	uint32_t uid;
	memcpy(&uid, &current_request_as_byte_array[3], sizeof(uint32_t));

	if (uid == 0 && current_request_as_byte_array[7] == 8 &&
	    current_request_as_byte_array[8] == 0) {
		return true;
	} else {
		return false;
	}
}

void seq_pop_poll(void) {
	if (is_current_request_empty()) {
		log_debug("Updating sequence");

		++_red_rs485_extension.slaves[master_current_slave_to_process].sequence;
	}

	pop_packet_from_slave_queue();

	// Poll next slave after the configured timeout
	arm_master_poll_slave_interval_timer();
}

void arm_master_poll_slave_interval_timer(void) {
	log_debug("Waiting before polling next slave");
	master_poll_interval = true;

	master_timer.it_interval.tv_sec = 0;
	master_timer.it_interval.tv_nsec = 0;
	master_timer.it_value.tv_sec = 0;
	master_timer.it_value.tv_nsec = MASTER_POLL_SLAVE_INTERVAL;
	timerfd_settime(_master_timer_event, 0, &master_timer, NULL);
	last_timer_enable_at_us = microtime();
}

// New packet from brickd event loop is queued to be sent via RS485 interface
int red_rs485_extension_dispatch_to_rs485(Stack *stack, Packet *request, Recipient *recipient) {
	RS485ExtensionPacket* queued_request;
	int i;

	(void)stack;

	if (request->header.uid == 0 || recipient == NULL) {
		log_packet_debug("Broadcasting to all available slaves");

		for (i = 0; i < _red_rs485_extension.slave_num; i++) {
			queued_request = queue_push(&_red_rs485_extension.slaves[i].packet_queue);

			if (queued_request == NULL) {
				log_error("Could not push request (%s) to packet queue for slave %d, dropping request: %s (%d)",
				          packet_get_request_signature(packet_signature, request),
				          _red_rs485_extension.slaves[i].address,
				          get_errno_name(errno), errno);

				return -1;
			}

			queued_request->tries_left = RS485_FRAME_TRIES_DATA;
			memcpy(&queued_request->packet, request, request->header.length);

			log_packet_debug("Broadcast... Packet is queued to be sent to slave %d. Function signature = (%s)",
			                 _red_rs485_extension.slaves[i].address,
			                 packet_get_request_signature(packet_signature, request));
		}
	} else if (recipient != NULL) {
		for (i = 0; i < _red_rs485_extension.slave_num; i++) {
			if (_red_rs485_extension.slaves[i].address == recipient->opaque) {
				queued_request = queue_push(&_red_rs485_extension.slaves[i].packet_queue);

				if (queued_request == NULL) {
					log_error("Could not push request (%s) to packet queue for slave %d, dropping request: %s (%d)",
					          packet_get_request_signature(packet_signature, request),
					          _red_rs485_extension.slaves[i].address,
					          get_errno_name(errno), errno);

					return -1;
				}

				queued_request->tries_left = RS485_FRAME_TRIES_DATA;
				memcpy(&queued_request->packet, request, request->header.length);

				log_packet_debug("Packet is queued to be sent to slave %d over. Function signature = (%s)",
				                 _red_rs485_extension.slaves[i].address,
				                 packet_get_request_signature(packet_signature, request));

				break;
			}
		}
	}

	return 0;
}

// Init function called from central brickd code
int red_rs485_extension_init(ExtensionRS485Config *rs485_config) {
	int phase = 0;
	bool cleanup_return_zero = false;
	int i;
	bool init_crc_error_count = false;

	log_info("Initializing extension subsystem");

	MASTER_POLL_SLAVE_INTERVAL = (uint64_t)config_get_option_value("poll_delay.rs485")->integer * 1000;

	// Create base stack
	if (stack_create(&_red_rs485_extension.base, "red_rs485_extension",
	                 red_rs485_extension_dispatch_to_rs485) < 0) {
		log_error("Could not create base stack for extension, %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// Add to stacks array
	if (hardware_add_stack(&_red_rs485_extension.base) < 0) {
		goto cleanup;
	}

	phase = 2;

	// Saving eeprom config
	_red_rs485_extension.address = rs485_config->address;
	_red_rs485_extension.baudrate = rs485_config->baudrate;

	_red_rs485_extension.parity = rs485_config->parity;
	_red_rs485_extension.stopbits = rs485_config->stopbits;

	if (rs485_config->address == 0) {
		_red_rs485_extension.slave_num = rs485_config->slave_num;

		for (i = 0; i < _red_rs485_extension.slave_num; i++) {
			_red_rs485_extension.slaves[i].address = rs485_config->slave_address[i];
			_red_rs485_extension.slaves[i].sequence = 0;

			if (queue_create(&_red_rs485_extension.slaves[i].packet_queue, sizeof(RS485ExtensionPacket)) < 0) {
				log_error("Could not create slave queue, %s (%d)",
				          get_errno_name(errno), errno);
				goto cleanup;
			}
		}
	} else {
		log_error("Only master mode supported");
		cleanup_return_zero = true;
		goto cleanup;
	}

	// Calculate time to send number of bytes of max packet length and to receive the same amount
	TIMEOUT = (((double)(TIMEOUT_BYTES / (double)(_red_rs485_extension.baudrate / 8)) *
	            (double)1000000000) * (double)2) + (double)8000000;

	// Configuring serial interface from the configs
	if (serial_interface_init(RS485_EXTENSION_SERIAL_DEVICE) < 0) {
		goto cleanup;
	}

	// Initial RS485 RX state
	init_rxe_pin_state(rs485_config->extension);

	phase = 3;

	// Adding serial data available event
	if (event_add_source(_red_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC,
	                     "rs485-serial", EVENT_READ,
	                     serial_data_available_handler, NULL) < 0) {
		log_error("Could not add new serial data event");

		goto cleanup;
	}

	phase = 4;

	// Setup master timer
	_master_timer_event = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

	if (!(_master_timer_event < 0)) {
		if (event_add_source(_master_timer_event, EVENT_SOURCE_TYPE_GENERIC,
		                     "rs485-timer", EVENT_READ, master_timeout_handler, NULL) < 0) {
			log_error("Could not add RS485 master timer notification pipe as event source");

			goto cleanup;
		}
	} else {
		log_error("Could not create RS485 master timer");

		goto cleanup;
	}

	phase = 5;

	// Get things going in case of a master with slaves configured
	if (_red_rs485_extension.slave_num > 0) {
		_initialized = true;
		log_info("Initialized as master");
		master_poll_slave();
	}

	init_crc_error_count = init_crc_error_count_to_fs();

	if (!init_crc_error_count || _red_rs485_extension.slave_num <= 0) {
		if(_red_rs485_extension.slave_num <= 0) {
			log_warn("No slaves configured");
		}

		cleanup_return_zero = true;

		if (!init_crc_error_count) {
			cleanup_return_zero = false;
		}

		goto cleanup;
	}

	phase = 6;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 5:
		event_remove_source(_master_timer_event, EVENT_SOURCE_TYPE_GENERIC);
		robust_close(_master_timer_event);
		// fall through

	case 4:
		event_remove_source(_red_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);
		robust_close(_red_rs485_serial_fd);
		// fall through

	case 3:
		if (_red_rs485_extension.address == 0) {
			for (i = 0; i < _red_rs485_extension.slave_num; i++) {
				queue_destroy(&_red_rs485_extension.slaves[i].packet_queue, NULL);
			}
		}

		// fall through

	case 2:
		hardware_remove_stack(&_red_rs485_extension.base);
		// fall through

	case 1:
		stack_destroy(&_red_rs485_extension.base);
		// fall through

	default:
		break;
	}

	if (cleanup_return_zero) {
		return 0;
	}

	return phase == 6 ? 0 : -1;
}

// Exit function called from central brickd code
void red_rs485_extension_exit(void) {
	int i;

	if (!_initialized) {
		return;
	}

	// Remove event as possible poll source
	event_remove_source(_red_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);
	event_remove_source(_master_timer_event, EVENT_SOURCE_TYPE_GENERIC);

	// We can also free the queue and stack now, nobody will use them anymore
	hardware_remove_stack(&_red_rs485_extension.base);
	stack_destroy(&_red_rs485_extension.base);

	// Close file descriptors
	robust_close(_red_rs485_serial_fd);
	robust_close(_master_timer_event);

	if (_red_rs485_extension.address == 0) {
		for (i = 0; i < _red_rs485_extension.slave_num; i++) {
			queue_destroy(&_red_rs485_extension.slaves[i].packet_queue, NULL);
		}
	}

	conf_file_destroy(&crc_error_count_file);
	timer_destroy(&crc_error_count_update_timer);
}

bool init_crc_error_count_to_fs(void) {
	ConfFileLine *line;
	char buffer[1024];

	// Create file
	if (conf_file_create(&crc_error_count_file) < 0) {
		log_error("Could not create RS485 CRC error count file: %s (%d)",
		          get_errno_name(errno), errno);

		return false;
	}

	// Write comment
	line = array_append(&crc_error_count_file.lines);

	if (line == NULL) {
		log_error("Could not add comment to RS485 CRC error count file: %s (%d)",
		          get_errno_name(errno), errno);

		conf_file_destroy(&crc_error_count_file);

		return false;
	}

	line->raw = strdup(RS485_EXTENSION_CRC_ERROR_COUNT_COMMENT);
	line->name = NULL;
	line->value = NULL;

	// Write options
	snprintf(buffer, sizeof(buffer), "%d", 0);

	if (conf_file_set_option_value(&crc_error_count_file, "crc_errors", buffer) < 0) {
		log_error("Could not set '%s' option for RS485 CRC error count file: %s (%d)",
		          "type", get_errno_name(errno), errno);

		conf_file_destroy(&crc_error_count_file);

		return false;
	}

	// Write config to filesystem
	if (conf_file_write(&crc_error_count_file, RS485_EXTENSION_CRC_ERROR_COUNT_FILE_PATH) < 0) {
		log_error("Could not write config to '%s': %s (%d)",
		          buffer, get_errno_name(errno), errno);

		return false;
	}

	// Setup and start CRC error count value update timer
	if (timer_create_(&crc_error_count_update_timer, update_crc_error_count_to_fs, &crc_error_count_value) < 0) {
		log_error("Could not create CRC error count update timer: %s (%d)",
		          get_errno_name(errno), errno);

		return false;
	}

	if (timer_configure(&crc_error_count_update_timer, 0, CRC_ERROR_COUNT_UPDATE_INTERVAL) < 0) {
		log_error("Could not start CRC error count update timer: %s (%d)",
		          get_errno_name(errno), errno);

		return false;
	}

	return true;
}

static void update_crc_error_count_to_fs(void *opaque) {
	char buffer[1024];
	uint64_t _crc_error_count_value = *((uint64_t *)opaque);

	// Write options
	snprintf(buffer, sizeof(buffer), "%d", (int)_crc_error_count_value);

	if (conf_file_set_option_value(&crc_error_count_file, "crc_errors", buffer) < 0) {
		log_error("Could not set '%s' option for RS485 CRC error count file: %s (%d)",
		          "type", get_errno_name(errno), errno);
	}

	// Write config to filesystem
	if (conf_file_write(&crc_error_count_file, RS485_EXTENSION_CRC_ERROR_COUNT_FILE_PATH) < 0) {
		log_error("Could not write config to '%s': %s (%d)",
		          buffer, get_errno_name(errno), errno);
	}

	log_debug("CRC error count updated, current value: %d", (int)_crc_error_count_value);
}
