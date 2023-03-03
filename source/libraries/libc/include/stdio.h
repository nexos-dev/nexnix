/*
    stdio.h - contains standard I/O stuff
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

#ifndef _STDIO_H
#define _STDIO_H

#define __NEED_SIZET
#include <bits/types.h>
#include <stdarg.h>

#define EOF          (-1)
#define FILENAME_MAX 512

int sprintf (char* restrict str, const char* fmt, ...);
int snprintf (char* restrict str, size_t n, const char* fmt, ...);
int vsprintf (char* restrict str, const char* fmt, va_list ap);
int vsnprintf (char* restrict str, size_t n, const char* fmt, va_list ap);

#endif
