/*
    console.h - contains console driver
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

#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <nexboot/driver.h>
#include <nexboot/fw.h>

// Console hardware driver stuff
#define NB_CONSOLEHW_NOTIFY_SETOWNER 32

#define NB_CONSOLEHW_CLEAR          5
#define NB_CONSOLEHW_PRINTCHAR      6
#define NB_CONSOLEHW_DISABLE_CURSOR 7
#define NB_CONSOLEHW_ENABLE_CURSOR  8
#define NB_CONSOLEHW_SET_FGCOLOR    9
#define NB_CONSOLEHW_SET_BGCOLOR    10
#define NB_CONSOLEHW_SCROLLDOWN     11

typedef struct _consolePrint
{
    int col;
    int row;
    char c;
} NbPrintChar_t;

#define NB_CONSOLE_COLOR_BLACK   0
#define NB_CONSOLE_COLOR_RED     1
#define NB_CONSOLE_COLOR_GREEN   2
#define NB_CONSOLE_COLOR_YELLOW  3
#define NB_CONSOLE_COLOR_BLUE    4
#define NB_CONSOLE_COLOR_MAGENTA 5
#define NB_CONSOLE_COLOR_CYAN    6
#define NB_CONSOLE_COLOR_WHITE   7

#endif
