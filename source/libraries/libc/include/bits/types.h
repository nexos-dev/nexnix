/*
    types.h - contains standard C types
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

#ifndef _BITS_TYPES_H
#define _BITS_TYPES_H

#ifdef __NEED_NULL
#undef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*) 0)
#endif
#endif

#ifdef __NEED_SIZET
// Ensure size_t hasn't been already defined
#ifndef __SIZET_DEFINED
#define __SIZET_DEFINED
typedef unsigned long int size_t;
#endif
#endif

#endif
