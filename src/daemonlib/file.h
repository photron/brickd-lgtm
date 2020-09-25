/*
 * daemonlib
 * Copyright (C) 2014, 2016-2017, 2019-2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * file.h: File based I/O device
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

#ifndef DAEMONLIB_FILE_H
#define DAEMONLIB_FILE_H

#include "io.h"

typedef struct {
	IO base;

	IOHandle handle;
} File;

int file_create(File *file, const char *name, int flags, int mode); // takes open flags
void file_destroy(File *file);

int file_read(File *file, void *buffer, int length);
int file_write(File *file, const void *buffer, int length);
int file_status(File *file, IOStatus *status);
off_t file_seek(File *file, off_t offset, int origin); // takes lseek origin

#endif // DAEMONLIB_FILE_H
