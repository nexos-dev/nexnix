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
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#define NEXTEST_NAME "lex"
#include <libnex/progname.h>
#include <nextest.h>

void _confSetFileName (const char* file);

int main()
{
    // Set up locale stuff
    setlocale (LC_ALL, "");
    setprogname ("lex");
    _confSetFileName ("testLex.testxt");
    lexState_t* state = _confLexInit ("testLex.testxt");
    _confToken_t* tok = NULL;
    tok = _confLex (state);
    TEST_ANON (tok->type, 4);
    TEST_ANON (tok->line, 10);
    tok = _confLex (state);
    TEST_ANON (tok->type, 5);
    tok = _confLex (state);
    TEST_ANON (tok->type, 7);
    tok = _confLex (state);
    TEST_ANON (tok->type, 6);
    tok = _confLex (state);
    TEST_ANON (tok->type, 14);
    tok = _confLex (state);
    TEST_ANON (tok->type, 9);
    TEST_ANON (tok->num, 25);
    TEST_ANON (tok->line, 12);
    tok = _confLex (state);
    TEST_ANON (tok->type, 9);
    TEST_ANON (tok->num, 0xAD8B2);
    TEST_ANON (tok->line, 14);
    tok = _confLex (state);
    TEST_ANON (tok->type, 9);
    TEST_ANON (tok->num, -34);
    TEST_ANON (tok->line, 16);
    tok = _confLex (state);
    TEST_ANON (tok->type, 8);
    TEST_ANON (tok->line, 18);
    TEST_BOOL_ANON (!strcmp (tok->semVal, "test2-test3_"));
    tok = _confLex (state);
    TEST_ANON (tok->type, 8);
    TEST_ANON (tok->line, 20);
    TEST_BOOL_ANON (!strcmp (tok->semVal, "23test"));
    tok = _confLex (state);
    TEST_ANON (tok->type, 11);
    TEST_ANON (tok->line, 22);
    TEST_BOOL_ANON (!c32cmp (tok->strVal, U"test t \\ '"));
    tok = _confLex (state);
    TEST_ANON (tok->type, 11);
    TEST_ANON (tok->line, 24);
    TEST_BOOL_ANON (!c32cmp (tok->strVal, U"test string en_US.UTF-8 $ \" \ntest"));
    tok = _confLex (state);
    TEST_ANON (tok->type, 12);
    _confLexDestroy (state);
    return 0;
}
