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

#include <bits/arch.h>

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
typedef _uaddr_t size_t;
#endif
#endif

#ifdef __NEED_PTRDIFFT
#ifndef __PTRDIFFT_DEFINED
#define __PTRDIFFT_DEFINED
typedef _saddr_t ptrdiff_t;
#endif
#endif

#ifdef __NEED_MAXALIGN_T
#ifndef __MAXALIGNT_DEFINED
#define __MAXALIGNT_DEFINED
typedef _uint64_t max_align_t;
#endif
#endif

#ifdef __NEED_WCHART
#ifndef __WCHART_DEFINED
#define __WCHART_DEFINED
typedef _uint16_t wchar_t;
#endif
#endif
