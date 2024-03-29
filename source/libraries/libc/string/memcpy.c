/*
    memcpy.c - contains memcpy for libc
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

#include <stdint.h>
#include <string.h>

void* memcpy (void* restrict dest, const void* restrict src, size_t n)
{
    const uint8_t* s = src;
    uint8_t* d = dest;
    // Check if it is aligned
    if (!((uintptr_t) d % 8) && !((uintptr_t) s % 8))
    {
        uint64_t* dw = dest;
        const uint64_t* sw = src;
        size_t numQwords = n >> 3;
        size_t diff = n % 8;
        while (numQwords--)
            *dw++ = *sw++;
        uint8_t* d2 = (uint8_t*) dw;
        uint8_t* s2 = (uint8_t*) sw;
        while (diff--)
            *d2++ = *s2++;
    }
    else if (!((uintptr_t) d % 4) && !((uintptr_t) s % 4))
    {
        uint32_t* dw = dest;
        const uint32_t* sw = src;
        size_t numDwords = n >> 2;
        size_t diff = n % 4;
        while (numDwords--)
            *dw++ = *sw++;
        uint8_t* d2 = (uint8_t*) dw;
        uint8_t* s2 = (uint8_t*) sw;
        while (diff--)
            *d2++ = *s2++;
    }
    else if (!((uintptr_t) d % 2) && !((uintptr_t) s % 2))
    {
        uint16_t* dw = dest;
        const uint16_t* sw = src;
        size_t numWords = n >> 1;
        size_t diff = n % 2;
        while (numWords--)
            *dw++ = *sw++;
        uint8_t* d2 = (uint8_t*) dw;
        uint8_t* s2 = (uint8_t*) sw;
        while (diff--)
            *d2++ = *s2++;
    }
    else
    {
        while (n--)
            *d++ = *s++;
    }
    return dest;
}
