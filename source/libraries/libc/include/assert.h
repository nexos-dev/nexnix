/*
    assert.h - contains assert macro
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

void __attribute__ ((noreturn))
__assert_failed (const char* expr, const char* file, int line, const char* func);

#ifdef NDEBUG
#define assert(expr) ((void) 0)
#else
#define assert(expr) (void) ((expr) ? 0 : __assert_failed (#expr, __FILE__, __LINE__, __func__))
#endif

#define static_assert _Static_assert
