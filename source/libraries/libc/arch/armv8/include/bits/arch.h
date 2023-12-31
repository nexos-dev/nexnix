/*
    arch.h - contains defines for armv8
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

#ifndef _BITS_ARCH_H
#define _BITS_ARCH_H

#define _saddr_t  long
#define _uaddr_t  unsigned long
#define _int64_t  long long
#define _uint64_t unsigned long long
#define _int32_t  int
#define _uint32_t unsigned int
#define _int16_t  short
#define _uint16_t unsigned short
#define _int8_t   char
#define _uint8_t  unsigned char
#ifndef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__ unsigned long long
#endif

#endif
