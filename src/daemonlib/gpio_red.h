/*
 * daemonlib
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gpio_red.h: GPIO functions for RED Brick
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

#ifndef DAEMONLIB_GPIO_RED_H
#define DAEMONLIB_GPIO_RED_H

#include <stdint.h>

typedef enum {
	GPIO_RED_PIN_0 = 0,
	GPIO_RED_PIN_1 = 1,
	GPIO_RED_PIN_2 = 2,
	GPIO_RED_PIN_3 = 3,
	GPIO_RED_PIN_4 = 4,
	GPIO_RED_PIN_5 = 5,
	GPIO_RED_PIN_6 = 6,
	GPIO_RED_PIN_7 = 7,
	GPIO_RED_PIN_8 = 8,
	GPIO_RED_PIN_9 = 9,
	GPIO_RED_PIN_10 = 10,
	GPIO_RED_PIN_11 = 11,
	GPIO_RED_PIN_12 = 12,
	GPIO_RED_PIN_13 = 13,
	GPIO_RED_PIN_14 = 14,
	GPIO_RED_PIN_15 = 15,
	GPIO_RED_PIN_16 = 16,
	GPIO_RED_PIN_17 = 17,
	GPIO_RED_PIN_18 = 18,
	GPIO_RED_PIN_19 = 19,
	GPIO_RED_PIN_20 = 20,
	GPIO_RED_PIN_21 = 21,
	GPIO_RED_PIN_22 = 22,
	GPIO_RED_PIN_23 = 23,
	GPIO_RED_PIN_24 = 24,
	GPIO_RED_PIN_25 = 25,
	GPIO_RED_PIN_26 = 26,
	GPIO_RED_PIN_27 = 27,
	GPIO_RED_PIN_28 = 28,
	GPIO_RED_PIN_29 = 29,
	GPIO_RED_PIN_30 = 30,
	GPIO_RED_PIN_31 = 31
} GPIOREDPinIndex;

typedef enum {
	GPIO_RED_PORT_A = 0,
	GPIO_RED_PORT_B = 1,
	GPIO_RED_PORT_C = 2,
	GPIO_RED_PORT_D = 3,
	GPIO_RED_PORT_E = 4,
	GPIO_RED_PORT_F = 5,
	GPIO_RED_PORT_G = 6,
	GPIO_RED_PORT_H = 7,
	GPIO_RED_PORT_I = 8,
} GPIOREDPortIndex;

typedef enum {
	GPIO_RED_INPUT_DEFAULT = 0,
	GPIO_RED_INPUT_PULLUP = 1,
	GPIO_RED_INPUT_PULLDOWN = 2
} GPIOREDInputConfig;

typedef enum {
	GPIO_RED_MUX_INPUT = 0,
	GPIO_RED_MUX_OUTPUT = 1,
	GPIO_RED_MUX_0 = 0,
	GPIO_RED_MUX_1 = 1,
	GPIO_RED_MUX_2 = 2,
	GPIO_RED_MUX_3 = 3,
	GPIO_RED_MUX_4 = 4,
	GPIO_RED_MUX_5 = 5,
	GPIO_RED_MUX_6 = 6,
} GPIOREDMux;

typedef struct {
	uint32_t config[4];
	uint32_t value;
	uint32_t multi_drive[2];
	uint32_t pull[2];
} GPIOREDPort;

typedef struct {
	GPIOREDPortIndex port_index;
	GPIOREDPinIndex pin_index;
} GPIOREDPin;

int gpio_red_init(void);
void gpio_red_mux_configure(const GPIOREDPin pin, const GPIOREDMux mux_config);
void gpio_red_input_configure(const GPIOREDPin pin, const GPIOREDInputConfig input_config);
void gpio_red_output_set(const GPIOREDPin pin);
void gpio_red_output_clear(const GPIOREDPin pin);
uint32_t gpio_red_input(const GPIOREDPin pin);

#endif // DAEMONLIB_GPIO_RED_H
