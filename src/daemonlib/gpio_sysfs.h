/*
 * daemonlib
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gpio_sysfs.h: GPIO functions for using Linux sysfs
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

#ifndef DAEMONLIB_GPIO_SYSFS_H
#define DAEMONLIB_GPIO_SYSFS_H

typedef enum {
	GPIO_SYSFS_INTERRUPT_NONE = 0,
	GPIO_SYSFS_INTERRUPT_RISING,
	GPIO_SYSFS_INTERRUPT_FALLING,
	GPIO_SYSFS_INTERRUPT_BOTH,
} GPIOSYSFSInterrupt;

typedef enum {
	GPIO_SYSFS_VALUE_LOW = 0,
	GPIO_SYSFS_VALUE_HIGH,
} GPIOSYSFSValue;

typedef enum {
	GPIO_SYSFS_DIRECTION_INPUT = 0,
	GPIO_SYSFS_DIRECTION_OUTPUT,
} GPIOSYSFSDirection;

typedef struct {
	char name[32];
	int num;
} GPIOSYSFS;

int gpio_sysfs_export(GPIOSYSFS *gpio);
int gpio_sysfs_unexport(GPIOSYSFS *gpio);
int gpio_sysfs_set_direction(GPIOSYSFS *gpio, GPIOSYSFSDirection direction);
int gpio_sysfs_set_output(GPIOSYSFS *gpio, GPIOSYSFSValue value);
int gpio_sysfs_get_input(GPIOSYSFS *gpio, GPIOSYSFSValue *value);
int gpio_sysfs_set_interrupt(GPIOSYSFS *gpio, GPIOSYSFSInterrupt interrupt);
int gpio_sysfs_get_input_fd(GPIOSYSFS *gpio);

#endif // DAEMONLIB_GPIO_SYSFS_H
