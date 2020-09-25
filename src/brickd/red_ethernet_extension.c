/*
 * brickd
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2019 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * red_ethernet_extension.c: Ethernet extension support for RED Brick
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
#include <sys/utsname.h>

#include <daemonlib/log.h>
#include <daemonlib/gpio_red.h>
#include <daemonlib/red_i2c_eeprom.h>
#include <daemonlib/utils.h>

#include "red_ethernet_extension.h"

#include "red_extension.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define W5X00_PATH_MAX_SIZE 256
#define W5X00_PARAM_MAX_SIZE 150
#define W5X00_MODULE_MAX_SIZE (1000*200)

#define EXTENSION_POS0_SELECT {GPIO_RED_PORT_G, GPIO_RED_PIN_9}
#define EXTENSION_POS1_SELECT {GPIO_RED_PORT_G, GPIO_RED_PIN_13}

extern int init_module(void *module_image, unsigned long len,
                       const char *param_values);
extern int delete_module(const char *name, int flags);

void red_ethernet_extension_rmmod(void) {
	if (delete_module("w5x00", 0) < 0) {
		// ENOENT = w5x00 was not loaded (which is OK)
		if (errno != ENOENT) {
			log_warn("Could not remove kernel module: %s (%d)",
			         get_errno_name(errno), errno);

			// In this error case we run through, maybe we
			// can load the kernel module anyway.
		}
	}
}

int red_ethernet_extension_init(ExtensionEthernetConfig *config) {
	struct utsname uts;
	FILE *fp;
	char buf_path[W5X00_PATH_MAX_SIZE + 1] = {0};
	char buf_param[W5X00_PARAM_MAX_SIZE + 1] = {0};
	char buf_module[W5X00_MODULE_MAX_SIZE];
	int param_pin_reset;
	int param_pin_interrupt;
	int param_select;
	int length;
	GPIOREDPin pin;

	log_debug("Initializing RED Brick Ethernet Extension subsystem");

	// Mux SPI CS pins again. They have been overwritten by I2C select!
	pin.port_index = GPIO_RED_PORT_G;

	switch (config->extension) {
	case 1:
#if BRICKD_WITH_RED_BRICK == 9
		param_pin_reset     = 20;
		param_pin_interrupt = 21;
#else
		// ((PORT_ALPHABET_INDEX - 1) * 32) + PIN_NR
		// Example: For PB5, ((2 - 1) * 32) + 5 = 37
		param_pin_reset     = 197; // PG05
		param_pin_interrupt = 195; // PG03
#endif
		param_select        = 1;
		pin.pin_index       = GPIO_RED_PIN_13; // CS1

		break;

	default:
		log_warn("Unsupported extension position (%d), assuming position 0",
		         config->extension);

		// Fallthrough

	case 0:
#if BRICKD_WITH_RED_BRICK == 9
		param_pin_reset     = 15;
		param_pin_interrupt = 17;
#else
		// ((PORT_ALPHABET_INDEX - 1) * 32) + PIN_NR
		// Example: For PB5, ((2 - 1) * 32) + 5 = 37
		param_pin_reset     = 45; // PB13
		param_pin_interrupt = 46; // PB14
#endif
		param_select        = 0;
		pin.pin_index       = GPIO_RED_PIN_9; // CS0

		break;
	}

	gpio_red_mux_configure(pin, GPIO_RED_MUX_2);

	if (uname(&uts) < 0) {
		log_error("Could not get kernel information: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	snprintf(buf_path,
	         W5X00_PATH_MAX_SIZE,
	         "/lib/modules/%s/kernel/drivers/net/ethernet/wiznet/w5x00.ko",
	         uts.release);

	snprintf(buf_param,
	         W5X00_PARAM_MAX_SIZE,
	         "param_pin_reset=%d param_pin_interrupt=%d param_select=%d param_mac=%d,%d,%d,%d,%d,%d",
	         param_pin_reset, param_pin_interrupt, param_select,
	         config->mac[0], config->mac[1], config->mac[2],
	         config->mac[3], config->mac[4], config->mac[5]);

	log_debug("Loading w5x00 kernel module for position %d [%s]",
	          config->extension, buf_param);

	fp = fopen(buf_path, "rb");

	if (fp == NULL) {
		log_error("Could not read w5x00 kernel module: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	length = robust_fread(fp, buf_module, W5X00_MODULE_MAX_SIZE);

	robust_fclose(fp);

	// We abort if the read was not successful or the buffer was not big enough
	if (length < 0 || length == W5X00_MODULE_MAX_SIZE) {
		log_error("Could not read %s (%d)", buf_path, length);

		return -1;
	}

	if (init_module(buf_module, length, buf_param) < 0) {
		log_error("Could not initialize w5x00 kernel module (length %d): %s (%d)",
		          length, get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void red_ethernet_extension_exit(void) {
	log_debug("Shutting down RED Brick Ethernet Extension subsystem");

	// Nothing to do here, we do not rmmod the module, if brickd
	// is closed. The Ethernet Extension may still be needed!
	// Example: Closing/recompiling/restarting brickd over ssh.
}
