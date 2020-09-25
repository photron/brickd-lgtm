#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import struct

if sys.hexversion < 0x03040000:
    print('Python 3.4 required')
    sys.exit(1)

BASE58 = '123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ'

def base58encode(value):
    encoded = ''

    while value >= 58:
        div, mod = divmod(value, 58)
        encoded = BASE58[mod] + encoded
        value = div

    return BASE58[value] + encoded

def main():
    path = sys.argv[1]

    with open(path, 'rb') as f:
        data = f.read()

    last_timestamp = None

    while len(data) > 0:
        trace_id, timestamp, header_uid, header_length, header_function_id, \
          header_sequence_number_and_options, header_error_code_and_future_use \
          = struct.unpack_from('<QQIBBBB', data, 0)

        data = data[24:]

        i = data.find(b'\0')
        filename = data[:i].decode('utf-8')

        data = data[i + 1:]

        line = struct.unpack_from('<i', data, 0)[0]

        data = data[4:]

        if last_timestamp == None:
            last_timestamp = timestamp

        print('I: {:20d}, T: {} {:+10d}, U: {:6}, L: {:3d}, F: {:3d}, S: {:2d}, R: {}, E: {} -> {}:{}'
              .format(trace_id,
                      timestamp,
                      timestamp - last_timestamp,
                      base58encode(header_uid),
                      header_length,
                      header_function_id,
                      header_sequence_number_and_options >> 4,
                      (header_sequence_number_and_options >> 2) & 1,
                      header_error_code_and_future_use >> 6,
                      filename,
                      line))

        last_timestamp = timestamp

if __name__ == '__main__':
    main()
