/*
 * daemonlib
 * Copyright (C) 2014 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2014-2015 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_i2c_eeprom.h: I2C EEPROM specific functions
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

#ifndef DAEMONLIB_I2C_EEPROM_H
#define DAEMONLIB_I2C_EEPROM_H

#include <stdint.h>

#include "gpio_red.h"

typedef struct {
	int extension;
	int file;
	GPIOREDPin address_pin;
} I2CEEPROM;

int i2c_eeprom_create(I2CEEPROM *i2c_eeprom, int extension);
void i2c_eeprom_destroy(I2CEEPROM *i2c_eeprom);

int i2c_eeprom_read(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                    uint8_t *buffer_to_store, int bytes_to_read);
int i2c_eeprom_write(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                     uint8_t *buffer_to_write, int bytes_to_write);

#endif // DAEMONLIB_I2C_EEPROM_H
