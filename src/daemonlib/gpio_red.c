/*
 * daemonlib
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2016, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gpio_red.c: GPIO functions for RED Brick
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
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "gpio_red.h"

#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define GPIO_RED_BASE 0x01c20800

static volatile GPIOREDPort *_gpio_red_port;

int gpio_red_init(void) {
	int fd;
	uint32_t address_start;
	uint32_t address_offset;
	uint32_t page_size;
	uint32_t page_mask;
	void *mapped_base;

	fd = open("/dev/mem", O_RDWR);

	if (fd < 0) {
		log_error("Could not open '/dev/mem': %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	page_size = sysconf(_SC_PAGESIZE);
	page_mask = ~(page_size - 1);
	address_start = GPIO_RED_BASE & page_mask;
	address_offset = GPIO_RED_BASE & ~page_mask;

	// FIXME: add gpio_red_exit() to munmap?
	mapped_base = mmap(0, page_size * 2, PROT_READ | PROT_WRITE, MAP_SHARED,
	                   fd, address_start);

	if (mapped_base == MAP_FAILED) {
		robust_close(fd);

		log_error("Could not mmap '/dev/mem': %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	_gpio_red_port = mapped_base + address_offset;

	robust_close(fd);

	return 0;
}

void gpio_red_mux_configure(const GPIOREDPin pin, const GPIOREDMux mux_config) {
	uint32_t config_index = pin.pin_index >> 3;
	uint32_t offset = (pin.pin_index & 0x7) << 2;
	uint32_t config = _gpio_red_port[pin.port_index].config[config_index];

	config &= ~(0xF << offset);
	config |= mux_config << offset;

	_gpio_red_port[pin.port_index].config[config_index] = config;
}

void gpio_red_input_configure(const GPIOREDPin pin, const GPIOREDInputConfig input_config) {
	uint32_t config_index = pin.pin_index >> 4;
	uint32_t offset = (pin.pin_index * 2) % 32;
	uint32_t config = _gpio_red_port[pin.port_index].pull[config_index];

	config &= ~(0x3 << offset);
	config |= input_config << offset;

	_gpio_red_port[pin.port_index].pull[config_index] = config;
}

void gpio_red_output_set(const GPIOREDPin pin) {
	_gpio_red_port[pin.port_index].value |= (1 << pin.pin_index);
}

void gpio_red_output_clear(const GPIOREDPin pin) {
	_gpio_red_port[pin.port_index].value &= ~(1 << pin.pin_index);
}

uint32_t gpio_red_input(const GPIOREDPin pin) {
	return _gpio_red_port[pin.port_index].value & (1 << pin.pin_index);
}
