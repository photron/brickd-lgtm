/*
 * brickd
 * Copyright (C) 2014-2018 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * red_usb_gadget.c: RED Brick USB gadget interface
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
 * this connects to the RED Brick g_red_brick USB gadget driver via the
 * /dev/g_red_brick_data and /proc/g_red_brick_state files. all TFP packets
 * received by the Brick API interface on the USB OTG connector are passed
 * through /dev/g_red_brick_data to brickd. all this data is then passed into
 * the brickd routing system as if it had been received from a normal TCP/IP
 * client.
 *
 * the RED Brick enumeration process is split into multiple locations.
 * the enumerate-connected packet is send from here to the USB gadget driver,
 * because only the brickd on the host side connected to the RED Brick should
 * receive the enumerate-connected for the RED Brick.
 *
 * the enumerate-available packet for the RED Brick is send by redapid.c that
 * connects to the RED Brick API Daemon and acts as a stack. this way all
 * clients connected to brickd can receive it.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <daemonlib/base58.h>
#include <daemonlib/event.h>
#include <daemonlib/file.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "red_usb_gadget.h"

#include "network.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define G_RED_BRICK_STATE_FILENAME "/proc/g_red_brick_state"
#define G_RED_BRICK_DATA_FILENAME "/dev/g_red_brick_data"

typedef enum {
	RED_USB_GADGET_STATE_DISCONNECTED = 0,
	RED_USB_GADGET_STATE_CONNECTED = 1
} REDUSBGadgetState;

static uint32_t _uid = 0; // always little endian
static File _state_file;
static Client *_client = NULL;

static int red_usb_gadget_create_client(void);

static void red_usb_gadget_client_destroy_done(void) {
	log_debug("Trying to reconnect to RED Brick USB gadget");

	_client = NULL;

	red_usb_gadget_create_client();
}

static int red_usb_gadget_create_client(void) {
	File *file;

	log_debug("Connecting to RED Brick USB gadget");

	file = calloc(1, sizeof(File));

	if (file == NULL) {
		log_error("Could not allocate file object: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return -1;
	}

	if (file_create(file, G_RED_BRICK_DATA_FILENAME,
	                O_RDWR | O_NONBLOCK, 0) < 0) {
		log_error("Could not create file object for '%s': %s (%d)",
		          G_RED_BRICK_DATA_FILENAME, get_errno_name(errno), errno);

		free(file);

		return -1;
	}

	_client = network_create_client("g_red_brick", &file->base);

	if (_client == NULL) {
		file_destroy(file);
		free(file);

		return -1;
	}

	_client->destroy_done = red_usb_gadget_client_destroy_done; // FIXME: this will only do one reconnect try
	_client->authentication_state = CLIENT_AUTHENTICATION_STATE_DISABLED;

	log_info("Connected to RED Brick USB gadget");

	return 0;
}

static int red_usb_gadget_connect(void) {
	// connect to /dev/g_red_brick_data
	if (red_usb_gadget_create_client() < 0) {
		return -1;
	}

	// send enumerate-connected callback
	log_debug("Sending enumerate-connected callback for RED Brick to '%s'",
	          G_RED_BRICK_DATA_FILENAME);

	client_send_red_brick_enumerate(_client, ENUMERATION_TYPE_CONNECTED);

	return 0;
}

static void red_usb_gadget_disconnect(void) {
	_client->destroy_done = NULL;
	_client->disconnected = true;
	_client = NULL;

	log_info("Disconnected from RED Brick USB gadget");
}

static void red_usb_gadget_handle_state_change(void *opaque) {
	uint8_t state;

	(void)opaque;

	log_debug("RED Brick USB gadget state changed");

	if (file_seek(&_state_file, SEEK_SET, 0) == (off_t)-1) {
		log_error("Could not seek '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		return;
	}

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		return;
	}

	switch (state) {
	case RED_USB_GADGET_STATE_CONNECTED:
		if (_client != NULL) {
			log_warn("Already connected to RED Brick USB gadget");

			return;
		}

		red_usb_gadget_connect();

		break;

	case RED_USB_GADGET_STATE_DISCONNECTED:
		if (_client == NULL) {
			log_warn("Already disconnected from RED Brick USB gadget");

			return;
		}

		red_usb_gadget_disconnect();

		break;

	default:
		log_warn("Unknown RED Brick USB gadget state %u", state);

		break;
	}
}

int red_usb_gadget_init(void) {
	int phase = 0;
	char base58[BASE58_MAX_LENGTH];
	uint8_t state;

	log_debug("Initializing RED Brick USB gadget subsystem");

	if (red_brick_uid(&_uid) < 0) {
		log_error("Could not get RED Brick UID: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	log_debug("Using %s (%u) as RED Brick UID",
	          base58_encode(base58, uint32_from_le(_uid)),
	          uint32_from_le(_uid));

	// read current USB gadget state from /proc/g_red_brick_state
	if (file_create(&_state_file, G_RED_BRICK_STATE_FILENAME,
	                O_RDONLY | O_NONBLOCK, 0) < 0) {
		log_error("Could not create file object for '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_state_file.handle, EVENT_SOURCE_TYPE_GENERIC, "usb-gadget",
	                     EVENT_READ, red_usb_gadget_handle_state_change, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	if (state == RED_USB_GADGET_STATE_CONNECTED && red_usb_gadget_connect() < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		event_remove_source(_state_file.handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 1:
		file_destroy(&_state_file);
		// fall through

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void red_usb_gadget_exit(void) {
	log_debug("Shutting down RED Brick USB gadget subsystem");

	if (_client != NULL) {
		red_usb_gadget_disconnect();
	}

	event_remove_source(_state_file.handle, EVENT_SOURCE_TYPE_GENERIC);

	file_destroy(&_state_file);
}

void red_usb_gadget_announce_red_brick_disconnect(void) {
	if (_client == NULL) {
		return;
	}

	log_debug("Sending enumerate-disconnected callback for RED Brick to '%s'",
	          G_RED_BRICK_DATA_FILENAME);

	client_send_red_brick_enumerate(_client, ENUMERATION_TYPE_DISCONNECTED);
}

uint32_t red_usb_gadget_get_uid(void) {
	return _uid;
}
