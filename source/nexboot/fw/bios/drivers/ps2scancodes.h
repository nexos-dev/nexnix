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

// Standard table for scan code set one
uint8_t scanToEnUs[] = 
{
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', PS2_KEY_CTRL,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', PS2_KEY_SHIFT, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', PS2_KEY_SHIFT, '*', PS2_KEY_ALT, ' ', PS2_KEY_CAPS_LOCK, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, '7', '8', '9', '-', '4',
    '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0
};

uint8_t scanToEnUsUppercase[] = 
{
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', PS2_KEY_CTRL,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', PS2_KEY_SHIFT, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', PS2_KEY_SHIFT, '*', PS2_KEY_ALT, ' ', PS2_KEY_CAPS_LOCK, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, '7', '8', '9', '-', '4',
    '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0
};

// Table for scan codes perfixed with 0xE0
uint8_t scanToEnUs2[] = 
{
    PS2_KEY_HOME, PS2_KEY_UP, PS2_KEY_PGUP, 0,
    PS2_KEY_LEFT, 0, PS2_KEY_RIGHT, 0, PS2_KEY_END,
    PS2_KEY_DOWN, PS2_KEY_PGDN, 0, PS2_KEY_DELETE
};

// Escape key table
const char* keyToEscCode[] = 
{
    "\e[5~", "\e[6~", "\e[A","\e[C","\e[B","\e[3~","\e[H","\e[D", "\e[F"
};

#endif
