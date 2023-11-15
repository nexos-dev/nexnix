/*
    terminal.h - contains console driver
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
#include <nexboot/object.h>

// Console hardware driver stuff
#define NB_CONSOLEHW_NOTIFY_SETOWNER 32

#define NB_CONSOLEHW_CLEAR          5
#define NB_CONSOLEHW_PRINTCHAR      6
#define NB_CONSOLEHW_DISABLE_CURSOR 7
#define NB_CONSOLEHW_ENABLE_CURSOR  8
#define NB_CONSOLEHW_SET_FGCOLOR    9
#define NB_CONSOLEHW_SET_BGCOLOR    10
#define NB_CONSOLEHW_SCROLL_DOWN    11
#define NB_CONSOLEHW_MOVE_CURSOR    12
#define NB_CONSOLEHW_GET_SIZE       13

typedef struct _consoleSz
{
    int cols;
    int rows;
} NbConsoleSz_t;

typedef struct _consolePrint
{
    int col;
    int row;
    char c;
} NbPrintChar_t;

typedef struct _consoleLoc
{
    int col;
    int row;
} NbConsoleLoc_t;

// Keyboard stuff
#define NB_KEYBOARD_NOTIFY_SETOWNER 32

#define NB_KEYBOARD_READ_KEY 5
#define NB_KEYBOARD_DISABLE  6
#define NB_KEYBOARD_ENABLE   7

// Key structure
typedef struct _keyData
{
    union
    {
        uint8_t c;              // Key character in ASCII
        const char* escCode;    // VT-100 escape code of key
    };
    bool isEscCode;    // Wheter this is an escape code or not
    bool isBreak;      // Key was released, not pressed
    int flags;         // Contains state of capitals, CTRL, SHIFT, and ALT
} NbKeyData_t;

#define NB_KEY_FLAG_CTRL  (1 << 0)
#define NB_KEY_FLAG_ALT   (1 << 1)
#define NB_KEY_FLAG_CAPS  (1 << 2)
#define NB_KEY_FLAG_SHIFT (1 << 3)

// Serial port stuff
#define NB_SERIAL_WRITE 5
#define NB_SERIAL_READ  6

#define NB_SERIAL_NOTIFY_SETOWNER 32

#define NB_CONSOLE_COLOR_BLACK   0
#define NB_CONSOLE_COLOR_RED     1
#define NB_CONSOLE_COLOR_GREEN   2
#define NB_CONSOLE_COLOR_YELLOW  3
#define NB_CONSOLE_COLOR_BLUE    4
#define NB_CONSOLE_COLOR_MAGENTA 5
#define NB_CONSOLE_COLOR_CYAN    6
#define NB_CONSOLE_COLOR_WHITE   7

// Terminal functions
#define NB_TERMINAL_WRITE   5
#define NB_TERMINAL_READ    6
#define NB_TERMINAL_SETOPTS 7
#define NB_TERMINAL_GETOPTS 8
#define NB_TERMINAL_CLEAR   9

// Terminal read argument
typedef struct _termRead
{
    char* buf;       // Buffer to read into
    size_t bufSz;    // Size of buffer
} NbTermRead_t;

// Terminal structure
typedef struct _term
{
    NbObject_t* outEnd;    // Output end of terminal
    NbObject_t* inEnd;     // Input end of terminal
    int numCols;           // Number of columns
    int numRows;           // Number of rows
    bool echo;             // Wheter read characters are echoed to console
    bool isPrimary;        // Is primary terminal
    // State fields. All zeroed by TerminalGetOpts
    int col;           // Column number
    int row;           // Current row
    char inBuf[16];    // Characters buffered to read
    int bufPos;
    bool foundCr;    // If serial abstractor read a CR, this flag is set so the code
                     // looks for a potential LF
    int escState;    // State of escape code state machine
    int escParams[16];    // Escape code parameters
    int escPos;           // Escape array current position
    int numSize;          // Number of digits in current number
    int backMax[2];    // Row column describing max spot to backspace to during read
    uint32_t echoc;    // Flags specifying certain characters not to echo
} NbTerminal_t;

#define TERM_NO_ECHO_BACKSPACE (1 << 0)

#endif
