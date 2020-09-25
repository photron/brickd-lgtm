/*
 * daemonlib
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * base58.c: Base58 functions
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
#include <string.h>

#include "base58.h"

static const char *_base58_alphabet =
	"123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";

char *base58_encode(char *base58, uint32_t value) {
	uint32_t digit;
	char reverse[BASE58_MAX_LENGTH];
	int i = 0;
	int k = 0;

	while (value >= 58) {
		digit = value % 58;
		reverse[i] = _base58_alphabet[digit];
		value = value / 58;
		++i;
	}

	reverse[i] = _base58_alphabet[value];

	for (k = 0; k <= i; ++k) {
		base58[k] = reverse[i - k];
	}

	for (; k < BASE58_MAX_LENGTH; ++k) {
		base58[k] = '\0';
	}

	return base58;
}

// sets errno on error
int base58_decode(uint32_t *value, const char *base58) {
	int i;
	const char *p;
	int k;
	uint32_t base = 1;

	*value = 0;
	i = strlen(base58) - 1;

	if (i < 0) {
		errno = EINVAL;

		return -1;
	}

	for (; i >= 0; --i) {
		p = strchr(_base58_alphabet, base58[i]);

		if (p == NULL) {
			errno = EINVAL;

			return -1;
		}

		k = p - _base58_alphabet;

		if (*value > UINT32_MAX - k * base) {
			errno = ERANGE;

			return -1;
		}

		*value += k * base;
		base *= 58;
	}

	return 0;
}
