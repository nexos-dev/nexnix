/*
    nextest.h - contains test driver stuff
    Copyright 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    There should be a copy of the License distributed in a file named
    LICENSE, if not, you may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef _NEXTEST_H
#define _NEXTEST_H

#include <stdio.h>

#ifndef NEXTEST_NAME
#error Please define NEXTEST_NAME in the test driver file
#endif

// Defines a test case. Prints out a message if the test fails
#define TEST(line, res, name)                                         \
    if ((line) != (res))                                              \
    {                                                                 \
        printf ("Test %s in driver %s FAILED\n", name, NEXTEST_NAME); \
        return 1;                                                     \
    }

// Defines a test that returns a boolean
#define TEST_BOOL(line, name)                                         \
    if (!(line))                                                      \
    {                                                                 \
        printf ("Test %s in driver %s FAILED\n", name, NEXTEST_NAME); \
        return 1;                                                     \
    }

// A test with no name
#define TEST_ANON(line, res) TEST (line, res, "\b")
// A boolean test with no name
#define TEST_BOOL_ANON(line) TEST_BOOL (line, "\b")

#endif
