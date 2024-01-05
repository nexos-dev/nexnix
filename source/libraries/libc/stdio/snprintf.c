/*
    snprintf.c - contains snprintf
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

#include <stdio.h>

int snprintf (char* restrict str, size_t n, const char* fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    int res = vsnprintf (str, n, fmt, ap);
    va_end (ap);
    return res;
}
