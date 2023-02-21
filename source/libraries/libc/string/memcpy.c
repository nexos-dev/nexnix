/*
    memcpy.c - contains memcpy for libc
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

#include <stdint.h>
#include <string.h>

void* memcpy (void* restrict dest, const void* restrict src, size_t n)
{
    const uint8_t* s = src;
    uint8_t* d = dest;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}
