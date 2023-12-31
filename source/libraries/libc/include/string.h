/*
    string.h - contains string functions for NexNix
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

#ifndef _STRING_H
#define _STRING_H

// Specify types we need
#define __NEED_NULL
#define __NEED_SIZET

#include <bits/types.h>

// Functions
void* memset (void* str, int ch, size_t count);
int memcmp (const void* s1, const void* s2, size_t n);
void* memcpy (void* restrict dest, const void* restrict src, size_t n);

int strcmp (const char* s1, const char* s2);
size_t strlen (const char* s);
char* strcpy (char* restrict s1, const char* restrict s2);
char* strcat (char* restrict s1, const char* restrict s2);
char* strchr (const char* s, int ch);

#endif
