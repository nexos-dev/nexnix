/*
    stddef.h - contains standard library stuff
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

#ifndef _STDDEF_H
#define _STDDEF_H

// Get standard types
#define __NEED_NULL
#define __NEED_SIZET
#define __NEED_PTRDIFFT
#define __NEED_MAX_ALIGNT
#define __NEED_WCHART
#include <bits/types.h>

#define offsetof(type, member) ((size_t) & (((type*) 0)->member))

#endif
