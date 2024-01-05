/*
    stdio.h - contains standard I/O stuff
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

#ifndef _STDIO_H
#define _STDIO_H

#define __NEED_SIZET
#define __NEED_SSIZE
#include <bits/types.h>
#include <stdarg.h>

// FILE structure
typedef struct __lcfile
{
    int unused;
} FILE;

#define EOF          (-1)
#define FILENAME_MAX 512
#define SEEK_CUR     1
#define SEEK_SET     2
#define SEEK_END     3

int sprintf (char* restrict str, const char* fmt, ...);
int snprintf (char* restrict str, size_t n, const char* fmt, ...);
int vsprintf (char* restrict str, const char* fmt, va_list ap);
int vsnprintf (char* restrict str, size_t n, const char* fmt, va_list ap);
int fprintf (FILE* restrict stream, const char* restrict fmt, ...);
int vfprintf (FILE* restrict stream, const char* restrict fmt, va_list ap);
int fflush (FILE* f);
int feof (FILE*);
FILE* fopen (const char* restrict, const char* restrict);
size_t fread (void* restrict, size_t, size_t, FILE* restrict);
size_t fwrite (const void* restrict, size_t, size_t, FILE* restrict);
int fclose (FILE* f);
long ftell (FILE*);
int fseek (FILE*, long, int);

#define stderr (void*) 1
#define stdout (void*) 2
#define stdin  (void*) 3

#endif
