/*
    memcmp.c - contains memcmp function for libc
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

int memcmp (const void* s1, const void* s2, size_t n)
{
    const uint8_t* _s1 = s1;
    const uint8_t* _s2 = s2;
    while (n && (*_s1 == *_s2))
    {
        ++_s1;
        ++_s2;
    }
    if (n)
        return *_s1 - *_s2;
    return 0;
}
