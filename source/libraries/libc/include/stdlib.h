/*
    stdlib.h - contains standard library stuff
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

#ifndef _STDLIB_H
#define _STDLIB_H

// Get standard types
#define __NEED_NULL
#define __NEED_SIZET
#define __NEED_PTRDIFFT
#include <bits/types.h>

// Definitions
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// Functions
void* calloc (size_t numBlocks, size_t blockSize);
void* malloc (size_t sz);
void* realloc (void* block, size_t newSz);
void free (void* block);
_Noreturn void abort();
_Noreturn void exit (int status);
_Noreturn void _Exit (int status);
int atoi (const char* s);
char* getenv (const char*);
int abs (int);

#endif
