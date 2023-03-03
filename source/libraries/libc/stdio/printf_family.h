/*
    printf_family.h - contains structures passed between printf functions
    Copyright 2023 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef _PRINTF_H
#define _PRINTF_H

#define __NEED_SIZET
#include <bits/types.h>
#include <stdarg.h>
#include <stdint.h>

// Contains printf out data
typedef struct _printfOut
{
    int (*out) (struct _printfOut*, char);    // Function to be called
    char* buf;                                // Buffer being written to
    size_t bufSize;                           // Size of buffer
    size_t bufPos;                            // Current position in buffer
    int charsPrinted;                         // Number of characters printed
} _printfOut_t;

// Contains printf format string part decoding
typedef struct _printfFmt
{
    union
    {
        uintmax_t udata;    // Data to be converted or printed
        intmax_t sdata;
        uintmax_t ptr;
    };
    int flags;        // Flags applied
    int width;        // Minimum neccesary width
    int precision;    // Minimum number of digits to appear on screen
    int conv;         // Conversion specifier
    int type;         // Type of data
} _printfFmt_t;

int vprintfCore (_printfOut_t* outData, const char* fmt, va_list ap);

#endif
