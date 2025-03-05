/* Copyright 2011, Jacques Fortier. All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted, with or without modification.
 * Modified by Toby Schneider to be header-only templated functions.
 */
#ifndef COBS_H
#define COBS_H

#include <cstddef>
#include <cstdint>

/* Stuffs "length" bytes of data at the location pointed to by
 * "input", writing the output to the location pointed to by
 * "output". Returns the number of bytes written to "output".
 * "Byte" can be any integer representation of an 8-bit byte such as std::uint8_t, std::int8_t or char
 */
template <typename Byte>
std::size_t cobs_encode(const Byte* input, std::size_t length, Byte* output)
{
    std::size_t read_index = 0;
    std::size_t write_index = 1;
    std::size_t code_index = 0;
    Byte code = 1;

    while (read_index < length)
    {
        if (input[read_index] == 0)
        {
            output[code_index] = code;
            code = 1;
            code_index = write_index++;
            read_index++;
        }
        else
        {
            output[write_index++] = input[read_index++];
            code++;
            if (code == 0xFF)
            {
                output[code_index] = code;
                code = 1;
                code_index = write_index++;
            }
        }
    }

    output[code_index] = code;

    return write_index;
}

/* Unstuffs "length" bytes of data at the location pointed to by
 * "input", writing the output * to the location pointed to by
 * "output". Returns the number of bytes written to "output" if
 * "input" was successfully unstuffed, and 0 if there was an
 * error unstuffing "input".
 * "Byte" can be any integer representation of an 8-bit byte such as std::uint8_t, std::int8_t or char
 */
template <typename Byte>
std::size_t cobs_decode(const Byte* input, std::size_t length, Byte* output)
{
    std::size_t read_index = 0;
    std::size_t write_index = 0;
    Byte code;
    Byte i;

    while (read_index < length)
    {
        code = input[read_index];

        if (read_index + code > length && code != 1)
        {
            return 0;
        }

        read_index++;

        for (i = 1; i < code; i++) { output[write_index++] = input[read_index++]; }
        if (code != 0xFF && read_index != length)
        {
            output[write_index++] = '\0';
        }
    }

    return write_index;
}

#endif
