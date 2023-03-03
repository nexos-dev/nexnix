/*
    log.c - contains logging abstractions
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

#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <string.h>

// The log herein is quite simple. It is themed after syslog, as described
// in RFC 3164. There are a few differences, but the ideas are the same

// Log structure format
typedef struct _nbLogEnt
{
    int priority;       // Contains facility and severity (but facility is always 0)
    short minute;       // Minute since boot of message
    short second;       // Seconds since boot of message
    short ms;           // Milliseconds since boot of message
    const char* msg;    // Message itself
} nbLogEntry_t;

// NOTE: Temporary, until we get dynamic memory allocation implemented
static nbLogEntry_t logEntries[256] = {0};

static short curEntry = 0;

static int minSeverity = 0;

void NbLogInit()
{
    // Convert log level to syslog severity
    if (NEXNIX_LOGLEVEL == 0)
        minSeverity = 0;
    else if (NEXNIX_LOGLEVEL == 1)
        minSeverity = NEXBOOT_LOGLEVEL_ERROR;
    else if (NEXNIX_LOGLEVEL == 2)
        minSeverity = NEXBOOT_LOGLEVEL_WARNING;
    else if (NEXNIX_LOGLEVEL == 3)
        minSeverity = NEXBOOT_LOGLEVEL_INFO;
    else
        minSeverity = NEXBOOT_LOGLEVEL_DEBUG;
}

void NbLogMessageEarly (const char* s, int level)
{
    // Log it
    logEntries[curEntry].priority = level;
    logEntries[curEntry].msg = s;
    // Check if we should print it
    if (level <= minSeverity)
    {
        while (*s)
        {
            NbFwEarlyPrint (*s);
            ++s;
        }
    }
}

void NbPrint (const char* s)
{
    while (*s)
    {
        NbFwEarlyPrint (*s);
        ++s;
    }
}
