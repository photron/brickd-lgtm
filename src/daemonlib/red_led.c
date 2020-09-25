/*
 * daemonlib
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2016, 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * red_led.c: LED functions for RED Brick
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

#include <stdio.h>
#include <string.h>

#include "red_led.h"

#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define LED_TRIGGER_NUM 6
#define LED_TRIGGER_STR_MAX_LENGTH 11
#define LED_PATH_STR_MAX_LENGTH 64

#define LED_TRIGGER_MAX_LENGTH 1024

static const char trigger_str[][LED_TRIGGER_STR_MAX_LENGTH] = {
	"cpu0",
	"gpio",
	"heartbeat",
	"mmc0",
	"none",
	"default-on"
};

static const char led_path[][LED_PATH_STR_MAX_LENGTH] = {
#if DAEMONLIB_WITH_RED_BRICK == 9
	"/sys/class/leds/pc05:green:status/trigger",
	"/sys/class/leds/pc06:red:error/trigger"
#else
	"/sys/class/leds/red-brick:led:running/trigger",
	"/sys/class/leds/red-brick:led:error/trigger"
#endif
};

int red_led_set_trigger(REDLED led, REDLEDTrigger trigger) {
	FILE *fp;
	int length;

	if (!((trigger >= RED_LED_TRIGGER_CPU) && (trigger <= RED_LED_TRIGGER_ON))) {
		log_error("Unknown LED trigger: %d (must be in [%d, %d])",
		          trigger, RED_LED_TRIGGER_CPU, RED_LED_TRIGGER_ON);

		return -1;
	}

	if (led > RED_LED_RED) {
		log_error("Unknown LED: %d (must be in [%d, %d])",
		          led, RED_LED_GREEN, RED_LED_RED);

		return -1;
	}

	fp = fopen(led_path[led], "w");

	if (fp == NULL) {
		log_error("Could not open file %s", led_path[led]);

		return -1;
	}

	length = fprintf(fp, "%s\n", trigger_str[trigger]);

	if (length < (int)strlen(trigger_str[trigger])) {
		robust_fclose(fp);

		log_error("Could not write to file %s", led_path[led]);

		return -1;
	}

	robust_fclose(fp);

	return 0;
}

REDLEDTrigger red_led_get_trigger(REDLED led) {
	char buf[LED_TRIGGER_MAX_LENGTH + 1] = {0};
	FILE *fp;
	int length;
	int i;

	if (led > RED_LED_RED) {
		log_error("Unknown LED: %d (must be in [%d, %d])",
		          led, RED_LED_GREEN, RED_LED_RED);

		return RED_LED_TRIGGER_UNKNOWN;
	}

	fp = fopen(led_path[led], "rb");

	if (fp == NULL) {
		log_error("Could not open file %s", led_path[led]);

		return RED_LED_TRIGGER_ERROR;
	}

	length = robust_fread(fp, buf, LED_TRIGGER_MAX_LENGTH);

	if (length <= 0) {
		robust_fclose(fp);

		log_error("Could not read from file %s", led_path[led]);

		return RED_LED_TRIGGER_ERROR;
	}

	buf[length] = '\0';

	robust_fclose(fp);

	char *start = strchr(buf, '[');
	char *end = strchr(buf, ']');

	if (start == NULL || end == NULL || start >= end) {
		return RED_LED_TRIGGER_UNKNOWN;
	}

	++start; // skip '['
	*end = '\0'; // overwrite ']'

	for (i = 0; i < LED_TRIGGER_NUM; i++) {
		if (strcmp(start, trigger_str[i]) == 0) {
			return i;
		}
	}

	return RED_LED_TRIGGER_UNKNOWN;
}
