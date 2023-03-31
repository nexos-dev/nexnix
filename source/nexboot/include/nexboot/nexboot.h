/*
    nexboot.h - contains basic booting functions
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

#ifndef _NEXBOOT_H
#define _NEXBOOT_H

#include <nexboot/object.h>

/// Log levels
#define NEXBOOT_LOGLEVEL_EMERGENCY 1
#define NEXBOOT_LOGLEVEL_CRITICAL  2
#define NEXBOOT_LOGLEVEL_ERROR     3
#define NEXBOOT_LOGLEVEL_WARNING   4
#define NEXBOOT_LOGLEVEL_NOTICE    5
#define NEXBOOT_LOGLEVEL_INFO      6
#define NEXBOOT_LOGLEVEL_DEBUG     7

/// Initializes log for bootup
void NbLogInit();

/// Disables early printing
void NbDisablePrintEarly();

/// Logs a string to the log, pre console intialization
void NbLogMessageEarly (const char* fmt, int level, ...);

// Prints a string using NbFwPrintEarly
void NbPrintEarly (const char* s);

/// Initializes memory manager
void NbMemInit();

// Minimum amount of memory NexNix requires (in MiB)
#define NEXBOOT_MIN_MEM 8

#endif
