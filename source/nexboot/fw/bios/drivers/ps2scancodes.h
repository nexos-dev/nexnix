/*
    ps2scancodes.h - contains PS/2 scan code tables
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

#ifndef _PS2SCANCODES_H
#define _PS2SCANCODES_H

#include <stddef.h>
#include <stdint.h>

// http://web.archive.org/web/20100725162026if_/http://www.computer-engineering.org/ps2keyboard/scancodes2.html

// Non ASCII or VT100 keys
#define PS2_KEY_ALT       0xFF
#define PS2_KEY_SHIFT     0xFE
#define PS2_KEY_CTRL      0xFD
#define PS2_KEY_CAPS_LOCK 0xFC
#define PS2_KEY_NUM_LOCK  0xFA
#define PS2_KEY_END       0xF9
#define PS2_KEY_LEFT      0xF8
#define PS2_KEY_HOME      0xF7
#define PS2_KEY_DELETE    0xF6
#define PS2_KEY_DOWN      0xF5
#define PS2_KEY_RIGHT     0xF4
#define PS2_KEY_UP        0xF3
#define PS2_KEY_PGDN      0xF2
#define PS2_KEY_PGUP      0xF1

// clang-format off

// Standard table for scan code set two
uint8_t scanToEnUs[] = 
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '\t', '`', 0, 0, PS2_KEY_ALT, PS2_KEY_SHIFT, 0,
    PS2_KEY_CTRL, 'q', '1', 0, 0, 0, 'z', 's', 'a', 'w',
    '2', 0, 0, 'c', 'x', 'd', 'e', '4', '3', 0,
    0, ' ', 'v', 'f', 't', 'r', '5', 0, 0, 'n',
    'b', 'h', 'g', 'y', '6', 0, 0, 0, 'm', 'j',
    'u', '7', '8', 0, 0, ',', 'k', 'i', 'o', '0',
    '9', 0, 0, '.', '/', 'l', ';', 'p', '-', 0, 0, 0,
    '\'', 0, '[', '=', 0, 0, PS2_KEY_CAPS_LOCK, PS2_KEY_SHIFT, '\n',
    ']', 0, '\\', 0, 0, 0, 0, 0, 0, 0, 0, '\b', 0, 0, '1', 0,
    '4', '7', 0, 0, 0, '0', '.', '2', '5', '6', '8', 0, PS2_KEY_NUM_LOCK,
    0, '+', '3', '-', '*', '9', 0, 0, 0, 0, 0, 0
};

uint8_t scanToEnUsUppercase[] = 
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '\t', '~', 0, 0, PS2_KEY_ALT, PS2_KEY_SHIFT, 0,
    PS2_KEY_CTRL, 'Q', '!', 0, 0, 0, 'Z', 'S', 'A', 'W',
    '@', 0, 0, 'C', 'X', 'D', 'E', '$', '#', 0,
    0, ' ', 'V', 'F', 'T', 'R', '%', 0, 0, 'N',
    'B', 'H', 'H', 'H', '^', 0, 0, 0, 'M', 'J',
    'U', '&', '*', 0, 0, '<', 'K', 'I', 'O', ')',
    '(', 0, 0, '>', '?', 'L', ':', 'P', '_', 0, 0, 0,
    '"', 0, '{', '+', 0, 0, PS2_KEY_CAPS_LOCK, PS2_KEY_SHIFT, '\n',
    '}', 0, '|', 0, 0, 0, 0, 0, 0, 0, 0, '\b', 0, 0, '1', 0,
    '4', '7', 0, 0, 0, '0', '.', '2', '5', '6', '8', 0, PS2_KEY_NUM_LOCK,
    0, '+', '3', '-', '*', '9', 0, 0, 0, 0, 0, 0
};

// Table for scan codes perfixed with 0xE0
uint8_t scanToEnUs2[] = 
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, PS2_KEY_ALT, 0, 0, PS2_KEY_CTRL, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, '/', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, PS2_KEY_END, 0, PS2_KEY_LEFT,
    PS2_KEY_HOME, 0, 0, 0, 0, PS2_KEY_DELETE, PS2_KEY_DOWN,
    0, PS2_KEY_RIGHT, PS2_KEY_UP, 0, 0, 0, 0, PS2_KEY_PGDN,
    0,0,
    PS2_KEY_PGUP
};

// Escape key table
const char* keyToEscCode[] = 
{
    NULL, NULL, "\eA","\eC","\eB","\x7F",NULL,"\eD", NULL
};

#endif
