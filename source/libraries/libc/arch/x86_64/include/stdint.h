/*
    stdint.h - contains standard integer stuff
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

#ifndef _STDINT_H
#define _STDINT_H

#include <bits/arch.h>

typedef _int8_t int8_t;
typedef _int16_t int16_t;
typedef _int32_t int32_t;
typedef _int64_t int64_t;
typedef _uint8_t uint8_t;
typedef _uint16_t uint16_t;
typedef _uint32_t uint32_t;
typedef _uint64_t uint64_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;
typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef int32_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;
typedef uint8_t uint_fast8_t;
typedef uint32_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

typedef int64_t intptr_t;
typedef uint64_t uintptr_t;

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

#define INT8_MIN  (-128)
#define INT8_MAX  127
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define INT32_MIN (-2147483648)
#define INT32_MAX 2147483647
#define INT64_MIN (-9223372036854775808)
#define INT64_MAX 9223372036854775807

#define INT_LEAST8_MIN  INT8_MIN
#define INT_LEAST8_MAX  INT8_MAX
#define INT_LEAST16_MIN INT16_MIN
#define INT_LEAST16_MAX INT16_MAX
#define INT_LEAST32_MIN INT32_MIN
#define INT_LEAST32_MAX INT32_MAX
#define INT_LEAST64_MIN INT64_MIN
#define INT_LEAST64_MAX INT64_MAX

#define INT_FAST8_MIN  INT8_MIN
#define INT_FAST8_MAX  INT8_MAX
#define INT_FAST16_MIN INT32_MIN
#define INT_FAST16_MAX INT32_MAX
#define INT_FAST32_MIN INT32_MIN
#define INT_FAST32_MAX INT32_MAX
#define INT_FAST64_MIN INT64_MIN
#define INT_FAST64_MAX INT64_MAX

#define UINT8_MAX  255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295
#define UINT64_MAX 18446744073709551615

#define UINT_LEAST8_MAX  UINT8_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#define UINT_LEAST64_MAX UINT64_MAX

#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX

#define UINTPTR_MAX UINT64_MAX

#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX

#define UINTMAX_MAX UINT64_MAX

#define SIZE_MAX UINT32_MAX

#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

#endif
