/*
    parse.c - contains parser test cases
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

/// @file parse.c

#include "../include/internal.h"
#include <locale.h>
#include <stdio.h>
#include <string.h>
#define NEXTEST_NAME "parse"
#include <libnex/progname.h>
#include <nextest.h>

int main()
{
    // Set up locale stuff
    setlocale (LC_ALL, "");
    setprogname ("parse");
    ListHead_t* list = ConfInit ("testParse.testxt");
    ListEntry_t* entry = NULL;
    ConfBlock_t* block = NULL;
    entry = ListPopFront (list);
    block = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (block->blockName, U"test"));
    TEST_BOOL_ANON (!c32cmp (block->blockType, U"package"));
    entry = ListPopFront (block->props);
    ConfProperty_t* prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"test"));
    for (int i = 0; i < prop->nextVal; ++i)
    {
        if (prop->vals[i].type == DATATYPE_IDENTIFIER)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].id, U"one"));
        }
        else if (prop->vals[i].type == DATATYPE_NUMBER)
        {
            TEST_ANON (prop->vals[i].numVal, 3);
        }
        else if (prop->vals[i].type == DATATYPE_STRING)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].str, U"test"));
        }
    }
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].id, U"propVal"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].str, U"string"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_ANON (prop->vals[0].numVal, 0x20);
    entry = ListPopFront (list);
    block = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (block->blockName, U"test"));
    TEST_BOOL_ANON (!c32cmp (block->blockType, U"package"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"test"));
    for (int i = 0; i < prop->nextVal; ++i)
    {
        if (prop->vals[i].type == DATATYPE_IDENTIFIER)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].id, U"one"));
        }
        else if (prop->vals[i].type == DATATYPE_NUMBER)
        {
            TEST_ANON (prop->vals[i].numVal, 3);
        }
        else if (prop->vals[i].type == DATATYPE_STRING)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].str, U"test"));
        }
    }
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].id, U"propVal"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].str, U"string"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_ANON (prop->vals[0].numVal, 0x20);
    entry = ListPopFront (list);
    block = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (block->blockName, U"test"));
    TEST_BOOL_ANON (!c32cmp (block->blockType, U"block"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"test"));
    for (int i = 0; i < prop->nextVal; ++i)
    {
        if (prop->vals[i].type == DATATYPE_IDENTIFIER)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].id, U"one"));
        }
        else if (prop->vals[i].type == DATATYPE_NUMBER)
        {
            TEST_ANON (prop->vals[i].numVal, 3);
        }
        else if (prop->vals[i].type == DATATYPE_STRING)
        {
            TEST_BOOL_ANON (!c32cmp (prop->vals[i].str, U"test"));
        }
    }
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].id, U"propVal"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_BOOL_ANON (!c32cmp (prop->vals[0].str, U"string"));
    entry = ListPopFront (block->props);
    prop = ListEntryData (entry);
    TEST_BOOL_ANON (!c32cmp (prop->name, U"prop"));
    TEST_ANON (prop->vals[0].numVal, 0x20);
    return 0;
}
