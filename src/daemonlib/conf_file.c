/*
 * daemonlib
 * Copyright (C) 2014-2015, 2017-2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * conf_file.c: Reads and writes .conf formatted files
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
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
	#include <windows.h>
#endif

#include "conf_file.h"

#ifdef _WIN32
	#define END_OF_LINE "\r\n"
#else
	#define END_OF_LINE "\n"
#endif

static int conf_file_unescape_string(int number, char *string, ConfFileReadWarningFunction warning, void *opaque) {
	char *p = string;
	char *d = string;
	char *s;
	char x[3];
	int tmp;

	while (*p != '\0') {
		if (*p < ' ' || *p > '~') {
			// reject any non-printable ASCII character
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_NON_PRINTABLE_ASCII_CHARACTER, number, p, opaque);
			}

			return -1;
		}

		// check for escape sequence start
		if (*p != '\\') {
			*d++ = *p++;

			continue;
		}

		s = p;

		++p; // skip backslash

		if (*p == '\0') {
			// end-of-line in the middle of an escape sequence
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_INCOMPLETE_ESCAPE_SEQUENCE, number, s, opaque);
			}

			return -1;
		}

		// check for common escape sequences
		switch (*p++) {
		case 'a':  *d++ = '\a'; continue;
		case 'b':  *d++ = '\b'; continue;
		case 'f':  *d++ = '\f'; continue;
		case 'n':  *d++ = '\n'; continue;
		case 'r':  *d++ = '\r'; continue;
		case 't':  *d++ = '\t'; continue;
		case 'v':  *d++ = '\v'; continue;
		case '\\': *d++ = '\\'; continue;
		case '\'': *d++ = '\''; continue;
		case '"':  *d++ = '"';  continue;
		case 'x':               break;
		default:
			// invalid escape sequence
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_INVALID_ESCAPE_SEQUENCE, number, s, opaque);
			}

			return -1;
		}

		if (*p == '\0' || *(p + 1) == '\0') {
			// end-of-line in the middle of an \x escape sequence
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_INCOMPLETE_ESCAPE_SEQUENCE, number, s, opaque);
			}

			return -1;
		}

		// unescape \x escape sequence. accept [\x01..\xFF] but explicitly
		// forbid \x00 (\0), because strings are NULL-terminated and cannot
		// contain a \0 character
		x[0] = *p++;
		x[1] = *p++;
		x[2] = '\0';

		if (parse_int(x, NULL, 16, &tmp) < 0) {
			// invalid number in \x escape sequence
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_INVALID_ESCAPE_SEQUENCE, number, s, opaque);
			}

			return -1;
		}

		if (tmp < 1 || tmp > 255) {
			// invalid number in \x escape sequence
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_INVALID_ESCAPE_SEQUENCE, number, s, opaque);
			}

			return -1;
		}

		*d++ = (char)tmp;
	}

	*d = '\0';

	return 0;
}

static int conf_file_write_escaped(FILE *fp, const char *string, bool name) {
	const char *p;
	bool printable;
	bool comment;
	bool whitespace;
	const char *start = NULL;
	char *escape;
	char buffer[16];

	for (p = string; *p != '\0'; ++p) {
		// check if printable ASCII character has to be encoded
		printable = *p >= ' ' && *p <= '~' && *p != '\\'; // is printable ASCII character (excluding backslash)?
		comment = *p == '#' && p == string; // is comment start?
		whitespace = *p == ' ' && (p == string || *(p + 1) == '\0'); // has leading/trailing whitespace?

		if (printable && !comment && !(*p == '=' && name) && !whitespace) {
			if (start == NULL) {
				start = p;
			}

			continue;
		}

		if (start != NULL) {
			if (robust_fwrite(fp, start, p - start) < 0) {
				return -1;
			}

			start = NULL;
		}

		// check for ASCII character with special escape sequence
		switch (*p) {
		case '\a': escape = "\\a";  break;
		case '\b': escape = "\\b";  break;
		case '\f': escape = "\\f";  break;
		case '\n': escape = "\\n";  break;
		case '\r': escape = "\\r";  break;
		case '\t': escape = "\\t";  break;
		case '\v': escape = "\\v";  break;
		case '\\': escape = "\\\\"; break;
		default:   escape = NULL;   break;
		}

		if (escape != NULL) {
			if (robust_fwrite(fp, escape, strlen(escape)) < 0) {
				return -1;
			}

			continue;
		}

		// represent everything else as \x escape sequence
		snprintf(buffer, sizeof(buffer), "\\x%02X", (uint8_t)*p);

		if (robust_fwrite(fp, buffer, strlen(buffer)) < 0) {
			return -1;
		}
	}

	if (start != NULL) {
		if (robust_fwrite(fp, start, p - start) < 0) {
			return -1;
		}
	}

	return 0;
}

static void conf_file_line_destroy(void *item) {
	ConfFileLine *line = item;

	free(line->raw);
	free(line->name);
	free(line->value);
}

// sets errno on error
static int conf_file_parse_line(ConfFile *conf_file, int number, char *buffer,
                                ConfFileReadWarningFunction warning, void *opaque) {
	char *raw;
	char *name = NULL;
	char *value = NULL;
	char *name_end;
	char *value_end;
	char *tmp1 = NULL;
	char *tmp2 = NULL;
	int rc;
	ConfFileLine *line;
	int saved_errno;

	// duplicate buffer, so it can be modified in-place
	raw = strdup(buffer);

	if (raw == NULL) {
		errno = ENOMEM;

		goto error;
	}

	// strip initial whitespace. the line can contain \r because only \n is used
	// as end-of-line marker. treat \r as regular whitespace
	name = buffer + strspn(buffer, " \t\r");

	// check for empty and comment lines
	if (*name != '\0' && *name != '#') {
		// split name and value
		value = strchr(name, '=');

		if (value == NULL) {
			if (warning != NULL) {
				warning(CONF_FILE_READ_WARNING_EQUAL_SIGN_MISSING, number, raw, opaque);
			}
		} else {
			name_end = value;
			*value++ = '\0';

			// remove trailing whitespace at end of name
			while (name_end > name && strspn(name_end - 1, " \t\r") > 0) {
				*--name_end = '\0';
			}

			if (*name == '\0') {
				if (warning != NULL) {
					warning(CONF_FILE_READ_WARNING_NAME_MISSING, number, raw, opaque);
				}
			} else {
				// strip whitespace around value
				value = value + strspn(value, " \t\r");
				value_end = value + strlen(value);

				while (value_end > value && strspn(value_end - 1, " \t\r") > 0) {
					*--value_end = '\0';
				}

				// duplicate name/value and unescape them
				tmp1 = strdup(name);
				tmp2 = strdup(value);

				if (tmp1 == NULL || tmp2 == NULL) {
					errno = ENOMEM;

					goto error;
				}

				rc = conf_file_unescape_string(number, tmp1, warning, opaque);

				if (rc >= 0) {
					rc = conf_file_unescape_string(number, tmp2, warning, opaque);

					if (rc >= 0) {
						name = tmp1;
						value = tmp2;

						// free raw
						free(raw);
						raw = NULL;
					}
				}
			}
		}
	}

	// add new line
	line = array_append(&conf_file->lines);

	if (line == NULL) {
		goto error;
	}

	line->raw = raw;

	if (raw == NULL) {
		line->name = name;
		line->value = value;
	} else {
		line->name = NULL;
		line->value = NULL;
	}

	return 0;

error:
	saved_errno = errno;

	free(raw);
	free(tmp1);
	free(tmp2);

	errno = saved_errno;

	return -1;
}

// sets errno on error
int conf_file_create(ConfFile *conf_file) {
	return array_create(&conf_file->lines, 32, sizeof(ConfFileLine), true);
}

void conf_file_destroy(ConfFile *conf_file) {
	array_destroy(&conf_file->lines, conf_file_line_destroy);
}

// sets errno on error
int conf_file_read(ConfFile *conf_file, const char *filename,
                   ConfFileReadWarningFunction warning, void *opaque) {
	bool success = false;
	int allocated = 256;
	FILE *fp = NULL;
	char *buffer = NULL;
	int rc;
	char c;
	int length = 0;
	bool skip = false;
	int number = 1;
	char *tmp;
	int i;
	ConfFileLine *line;

	// open file
	fp = fopen(filename, "rb");

	if (fp == NULL) {
		goto cleanup;
	}

	// allocate buffer
	buffer = malloc(allocated);

	if (buffer == NULL) {
		errno = ENOMEM;

		goto cleanup;
	}

	*buffer = '\0';

	// read and parse lines
	for (;;) {
		rc = robust_fread(fp, &c, 1);

		if (rc < 0) {
			goto cleanup;
		}

		if (rc == 0) {
			// use \0 to indicate end-of-file. this also ensures that parsing
			// stops on the first \0 character in the file
			c = '\0';
		}

		if (c == '\0' || c == '\n') {
			// end-of-file or end-of-line found. only use \n as end-of-line
			// marker. don't care about \r-only systems
			if (!skip) {
				// remove trailing \r if line ends with \r\n sequence
				if (length > 0 && buffer[length - 1] == '\r' && c == '\n') {
					buffer[--length] = '\0';
				}

				if (conf_file_parse_line(conf_file, number, buffer,
				                         warning, opaque) < 0) {
					goto cleanup;
				}
			}

			if (c == '\0') {
				// end-of-file reached
				break;
			}

			*buffer = '\0';
			length = 0;
			skip = false;
			++number;
		} else if (!skip) {
			if (length + 2 > allocated) {
				// not enough room for char and NULL-terminator
				if (allocated < 32768) {
					tmp = realloc(buffer, allocated * 2);

					if (tmp == NULL) {
						errno = ENOMEM;

						goto cleanup;
					}

					buffer = tmp;
					allocated *= 2;
				} else {
					// line is too long, skip it
					skip = true;

					if (warning != NULL) {
						// limit printed line length in log messages
						buffer[32] = '\0';

						warning(CONF_FILE_READ_WARNING_LINE_TOO_LONG,
						        number, buffer, opaque);
					}
				}
			}

			if (!skip) {
				buffer[length++] = c;
				buffer[length] = '\0';
			}
		}
	}

	// remove trailing empty lines
	for (i = conf_file->lines.count - 1; i >= 0; --i) {
		line = array_get(&conf_file->lines, i);

		if (line->raw == NULL || *line->raw != '\0') {
			break;
		}

		array_remove(&conf_file->lines, i, conf_file_line_destroy);
	}

	success = true;

cleanup:
	robust_fclose(fp);
	free(buffer);

	return success ? 0 : -1;
}

// sets errno on error
int conf_file_write(ConfFile *conf_file, const char *filename) {
	bool success = false;
	char filename_tmp[1024];
	FILE *fp = NULL;
	int i;
	ConfFileLine *line;

	if (robust_snprintf(filename_tmp, sizeof(filename_tmp), "%s.tmp", filename) < 0) {
		goto cleanup;
	}

	// open <filename>.tmp for writing
	fp = fopen(filename_tmp, "wb"); // FIXME: don't create as 0666 in all cases

	if (fp == NULL) {
		goto cleanup;
	}

	// write lines to <filename>.tmp
	for (i = 0; i < conf_file->lines.count; ++i) {
		line = array_get(&conf_file->lines, i);

		// if raw is != NULL then this line does not contain a name/value pair
		if (line->raw != NULL) {
			if (robust_fwrite(fp, line->raw, strlen(line->raw)) < 0) {
				goto cleanup;
			}
		} else {
			if (conf_file_write_escaped(fp, line->name, true) < 0) {
				goto cleanup;
			}

			if (robust_fwrite(fp, " =", 2) < 0) {
				goto cleanup;
			}

			if (*line->value != '\0') {
				if (robust_fwrite(fp, " ", 1) < 0) {
					goto cleanup;
				}

				if (conf_file_write_escaped(fp, line->value, false) < 0) {
					goto cleanup;
				}
			}
		}

		if (robust_fwrite(fp, END_OF_LINE, strlen(END_OF_LINE)) < 0) {
			goto cleanup;
		}
	}

	robust_fclose(fp);
	fp = NULL;

	// rename <filename>.tmp to <filename>. use MoveFileEx on Windows instead
	// of rename, because rename cannot replace existing files on Windows
#ifdef _WIN32
	if (!MoveFileExA(filename_tmp, filename, MOVEFILE_REPLACE_EXISTING)) {
		errno = ERRNO_WINAPI_OFFSET + GetLastError();

		goto cleanup;
	}
#else
	if (rename(filename_tmp, filename) < 0) {
		goto cleanup;
	}
#endif

	success = true;

cleanup:
	robust_fclose(fp);

	return success ? 0 : -1;
}

// sets errno on error
int conf_file_set_option_value(ConfFile *conf_file, const char *name, const char *value) {
	int i;
	ConfFileLine *line;
	char *tmp1;
	char *tmp2;
	int saved_errno;

	// iterate backwards to always change the latest instances of the same option
	for (i = conf_file->lines.count - 1; i >= 0; --i) {
		line = array_get(&conf_file->lines, i);

		if (line->name != NULL && strcasecmp(line->name, name) == 0) {
			tmp1 = strdup(value);

			if (tmp1 == NULL) {
				errno = ENOMEM;

				return -1;
			}

			free(line->value);
			line->value = tmp1;

			return 0;
		}
	}

	// not found, append new line
	tmp1 = strdup(name);
	tmp2 = strdup(value);

	if (tmp1 == NULL || tmp2 == NULL) {
		free(tmp1);
		free(tmp2);

		errno = ENOMEM;

		return -1;
	}

	line = array_append(&conf_file->lines);

	if (line == NULL) {
		saved_errno = errno;

		free(tmp1);
		free(tmp2);

		errno = saved_errno;

		return -1;
	}

	line->raw = NULL;
	line->name = tmp1;
	line->value = tmp2;

	return 0;
}

const char *conf_file_get_option_value(ConfFile *conf_file, const char *name) {
	int i;
	ConfFileLine *line;

	// iterate backwards to make later instances of the same option
	// override earlier instances
	for (i = conf_file->lines.count - 1; i >= 0; --i) {
		line = array_get(&conf_file->lines, i);

		if (line->name != NULL && strcasecmp(line->name, name) == 0) {
			return line->value;
		}
	}

	return NULL;
}

bool conf_file_get_first_option(ConfFile *conf_file, const char **name,
                                const char **value, int *cookie) {
	*cookie = 0;

	return conf_file_get_next_option(conf_file, name, value, cookie);
}

bool conf_file_get_next_option(ConfFile *conf_file, const char **name,
                               const char **value, int *cookie) {
	int i;
	ConfFileLine *line;

	for (i = *cookie; i >= 0 && i < conf_file->lines.count; ++i) {
		line = array_get(&conf_file->lines, i);

		if (line->raw != NULL) {
			continue;
		}

		*name = line->name;
		*value = line->value;
		*cookie = i + 1;

		return true;
	}

	*cookie = conf_file->lines.count;

	return false;
}

void conf_file_remove_option(ConfFile *conf_file, const char *name, bool prefix_match) {
	int i;
	ConfFileLine *line;
	int prefix_length = prefix_match ? strlen(name) : 0;

	// iterate backwards so that line removal doesn't affect the iteration index
	for (i = conf_file->lines.count - 1; i >= 0; --i) {
		line = array_get(&conf_file->lines, i);

		if (line->raw != NULL) {
			continue;
		}

		if ((prefix_match && strncasecmp(line->name, name, prefix_length) == 0) ||
		    strcasecmp(line->name, name) == 0) {
			array_remove(&conf_file->lines, i, conf_file_line_destroy);
		}
	}
}
