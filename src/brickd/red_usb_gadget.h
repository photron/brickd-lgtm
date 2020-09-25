/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * red_usb_gadget.h: RED Brick USB gadget interface
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

#ifndef BRICKD_RED_USB_GADGET_H
#define BRICKD_RED_USB_GADGET_H

#include <stdint.h>

#define RED_BRICK_DEVICE_IDENTIFIER 17

int red_usb_gadget_init(void);
void red_usb_gadget_exit(void);

void red_usb_gadget_announce_red_brick_disconnect(void);

uint32_t red_usb_gadget_get_uid(void);

#endif // BRICKD_RED_USB_GADGET_H
