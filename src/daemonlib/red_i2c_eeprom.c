/*
 * daemonlib
 * Copyright (C) 2014, 2018 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2014-2018 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_i2c_eeprom.c: I2C EEPROM specific functions
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
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "red_i2c_eeprom.h"

#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define I2C_EEPROM_BUS "/dev/i2c-2"
#define I2C_EEPROM_DEVICE_ADDRESS 0x54

static void i2c_eeprom_select(I2CEEPROM *i2c_eeprom) {
	gpio_red_output_set(i2c_eeprom->address_pin); // address pin high
}

static void i2c_eeprom_deselect(I2CEEPROM *i2c_eeprom) {
	gpio_red_output_clear(i2c_eeprom->address_pin); // address pin low
}

static int i2c_eeprom_set_pointer(I2CEEPROM *i2c_eeprom,
                                  uint8_t *eeprom_memory_address) {
	int bytes_written = 0;

	if (i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized");

		return -1;
	}

	bytes_written = robust_write(i2c_eeprom->file, eeprom_memory_address, 2);

	if (bytes_written != 2) {
		// We only use debug here to not spam the log with errors.
		// This is the expected case if an extension is not present.
		log_debug("Error setting EEPROM address pointer: %s (%d)",
		          get_errno_name(errno), errno);

		i2c_eeprom_destroy(i2c_eeprom);

		return -1;
	}

	return bytes_written;
}

// TODO: If we want "real parallel accessibility" of the EEPROM we need to
//       lock a mutex in the init function and unlock it in the release function
int i2c_eeprom_create(I2CEEPROM *i2c_eeprom, int extension) {
	GPIOREDPin pullup = {GPIO_RED_PORT_B, GPIO_RED_PIN_6};

	log_debug("Initializing I2C EEPROM for extension %d", extension);

	if (i2c_eeprom == NULL || extension < 0 || extension > 1) {
		log_error("Initialization of I2C EEPROM for extension %d failed (malformed parameters)",
		          extension);

		return -1;
	}

	// Enable pullups
	gpio_red_mux_configure(pullup, GPIO_RED_MUX_OUTPUT);
	gpio_red_output_clear(pullup);

	// Initialize I2C EEPROM structure
	i2c_eeprom->extension = extension;

	switch (extension) {
	case 0:
		i2c_eeprom->address_pin.port_index = GPIO_RED_PORT_G;
		i2c_eeprom->address_pin.pin_index = GPIO_RED_PIN_9;

		break;

	case 1:
		i2c_eeprom->address_pin.port_index = GPIO_RED_PORT_G;
		i2c_eeprom->address_pin.pin_index = GPIO_RED_PIN_13;

		break;
	}

	// enable I2C bus with GPIO_RED
	gpio_red_mux_configure(i2c_eeprom->address_pin, GPIO_RED_MUX_OUTPUT);
	i2c_eeprom_deselect(i2c_eeprom);

	i2c_eeprom->file = open(I2C_EEPROM_BUS, O_RDWR);

	if (i2c_eeprom->file < 0) {
		log_error("Initialization of I2C EEPROM for extension %d failed (Unable to open I2C bus: %s (%d))",
		          extension, get_errno_name(errno), errno);

		return -1;
	}

	if (ioctl(i2c_eeprom->file, I2C_SLAVE, I2C_EEPROM_DEVICE_ADDRESS) < 0) {
		log_error("Initialization of I2C EEPROM for extension %d failed (Unable to access I2C device on the bus: %s (%d))",
		          extension, get_errno_name(errno), errno);

		i2c_eeprom_destroy(i2c_eeprom);

		return -1;
	}

	return 0;
}

void i2c_eeprom_destroy(I2CEEPROM *i2c_eeprom) {
	log_debug("Releasing I2C EEPROM for extension %d", i2c_eeprom->extension);

	if (i2c_eeprom != NULL) {
		i2c_eeprom_deselect(i2c_eeprom);
		robust_close(i2c_eeprom->file);
		i2c_eeprom->file = -1;
	}
}

int i2c_eeprom_read(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                    uint8_t *buffer_to_store, int bytes_to_read) {
	int bytes_read = 0;
	uint8_t mem_address[2] = {eeprom_memory_address >> 8,
		                  eeprom_memory_address & 0xFF};

	if (i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized\n");
		return -1;
	}

	i2c_eeprom_select(i2c_eeprom);

	if (i2c_eeprom_set_pointer(i2c_eeprom, mem_address) < 0) {
		return -1;
	}

	bytes_read = robust_read(i2c_eeprom->file, buffer_to_store, bytes_to_read);

	if (bytes_read != bytes_to_read) {
		log_error("EEPROM read failed: %s (%d)", get_errno_name(errno), errno);

		i2c_eeprom_destroy(i2c_eeprom);
		return -1;
	}

	i2c_eeprom_deselect(i2c_eeprom);

	return bytes_read;
}

int i2c_eeprom_write(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                     uint8_t *buffer_to_write, int bytes_to_write) {
	int i;
	int rc;
	char bytes_written = 0;
	uint8_t write_byte[3] = {0};

	if (i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized\n");
		return -1;
	}

	for (i = 0; i < bytes_to_write; i++) {
		write_byte[0] = eeprom_memory_address >> 8;
		write_byte[1] = eeprom_memory_address & 0xFF;
		write_byte[2] = buffer_to_write[i];

		i2c_eeprom_select(i2c_eeprom);
		rc = robust_write(i2c_eeprom->file, write_byte, 3);
		i2c_eeprom_deselect(i2c_eeprom);

		// Wait at least 5ms between writes (see m24128-bw.pdf)
		usleep(5*1000);

		if (rc != 3) {
			log_error("EEPROM write failed (pos(%d), length(%d), expected length(%d): %s (%d)",
			          i, rc, 3, get_errno_name(errno), errno);

			i2c_eeprom_destroy(i2c_eeprom);

			return -1;
		}

		eeprom_memory_address++;
		bytes_written++;
	}

	return bytes_written;
}
