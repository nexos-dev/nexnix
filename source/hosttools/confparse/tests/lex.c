/*
    lex.c - contains lexer test case
    Copyright 2022 The NexNix Project

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

/// @file lex.c

#include "../include/internal.h"
#include <locale.h>
#include <stdio.h>
#define NEXTEST_NAME "lex"
#include <libnex/progname.h>
#include <nextest.h>

int main()
{
    // Set up locale stuff
    setlocale (LC_ALL, "");
    setprogname ("lex");
    bindtextdomain ("nexnix_tools", TOOLS_LOCALE_BASE);
    textdomain ("nexnix_tools");

    ConfInit ("testLex.testxt");
    _confLexInit ("testLex.testxt");
    _confLex();
    _confLexDestroy();
    return 0;
}
