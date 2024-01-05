/*
    vsnprintf.c - contains vsnprintf
    Copyright 2023 - 2024 The NexNix Project

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

#include "printf_family.h"
#include <stdio.h>

static int _printOut (_printfOut_t* outData, char c)
{
    // Ensure this won't overflow
    if (outData->bufPos == outData->bufSize)
        return EOF;
    outData->buf[outData->bufPos] = c;
    ++outData->bufPos;
    ++outData->charsPrinted;
    return 0;
}

int vsnprintf (char* restrict str, size_t n, const char* fmt, va_list ap)
{
    _printfOut_t out;
    // Prepare output structures
    out.out = _printOut;
    out.buf = str;
    out.bufSize = n;
    out.bufPos = 0;
    out.charsPrinted = 0;
    int res = vprintfCore (&out, fmt, ap);
    // Null terminate
    out.buf[out.bufPos] = 0;
    return res;
}
