/*
    log.c - contains system log
    Copyright 2024 The NexNix Project

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

#include <assert.h>
#include <nexke/cpu.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Log entry structure
typedef struct _logentry
{
    char msg[128];             // Message buffer
    int logLevel;              // Loglevel of message
    struct _logentry* next;    // Next log entry in list
} NkLogEntry_t;

// Minimum printable loglevel
static int loglevel;

// Cache it
static SlabCache_t* logCache = NULL;

// Start of entry list
static NkLogEntry_t* entryList = NULL;

// Tail of list
static NkLogEntry_t* entryListTail = NULL;

// Assert implementation
void __attribute__ ((noreturn))
__assert_failed (const char* expr, const char* file, int line, const char* func)
{
    NkPanic ("Assertion '%s' failed: file %s, line %d, functions %s", expr, file, line, func);
}

// Logs a message
void NkLogMessage (const char* fmt, int level, va_list ap)
{
    NkLogEntry_t* newEntry = MmCacheAlloc (logCache);
    if (!newEntry)
        NkPanicOom();
    newEntry->logLevel = level;
    vsprintf (newEntry->msg, fmt, ap);
    // Start list if needed
    if (!entryList)
        entryList = newEntry;
    // Add to tail
    if (entryListTail)
        entryListTail->next = newEntry;
    entryListTail = newEntry;
    // Decide where to print it
    if (level <= loglevel)
        PltGetPrimaryCons()->write (newEntry->msg);
    // Always print to secondary console if one is available
    if (PltGetSecondaryCons())
        PltGetSecondaryCons()->write (newEntry->msg);
}

// Initializes kernel log
void NkLogInit()
{
    // Ensure we have a console
    if (!PltGetPrimaryCons())
        CpuCrash();    // Just halt
    // Set loglevel
    const char* logLevelStr = NkReadArg ("-loglevel");
    if (!logLevelStr)
        loglevel = NK_LOGLEVEL_ERROR;
    else if (!(*logLevelStr))
    {
        // Print out a warning
        PltGetPrimaryCons()->write ("nexke: loglevel, argument invalid, ignoring\n");
        loglevel = NK_LOGLEVEL_ERROR;
    }
    loglevel = atoi (logLevelStr);
    // Create a log entry cache
    logCache = MmCacheCreate (sizeof (NkLogEntry_t), NULL, NULL);
    // Validate loglevel
    if (loglevel == 0)
    {
        PltGetPrimaryCons()->write ("nexke: error: loglevel must be at least 1");
        CpuCrash();
    }
    if (loglevel > 4)
    {
        PltGetPrimaryCons()->write ("nexke: error: loglevel value invalid (must be 1 - 4)");
        CpuCrash();
    }
    // Convert to actual loglevel
    if (loglevel == 1)
        loglevel = NK_LOGLEVEL_ERROR;
    else if (loglevel == 2)
        loglevel = NK_LOGLEVEL_WARNING;
    else if (loglevel == 3)
        loglevel = NK_LOGLEVEL_INFO;
    else if (loglevel == 4)
        loglevel = NK_LOGLEVEL_DEBUG;
    else
        assert (!"Invalid loglevel");
}

void __attribute__ ((noreturn)) NkPanic (const char* fmt, ...)
{
    // Log the message
    va_list ap;
    va_start (ap, fmt);
    NkLogMessage (fmt, NK_LOGLEVEL_EMERGENCY, ap);
    va_end (ap);
    // Crash
    CpuCrash();
}

void NkLogInfo (const char* fmt, ...)
{
    // Log the message
    va_list ap;
    va_start (ap, fmt);
    NkLogMessage (fmt, NK_LOGLEVEL_INFO, ap);
    va_end (ap);
}

void NkLogWarning (const char* fmt, ...)
{
    // Log the message
    va_list ap;
    va_start (ap, fmt);
    NkLogMessage (fmt, NK_LOGLEVEL_WARNING, ap);
    va_end (ap);
}

void NkLogError (const char* fmt, ...)
{
    // Log the message
    va_list ap;
    va_start (ap, fmt);
    NkLogMessage (fmt, NK_LOGLEVEL_ERROR, ap);
    va_end (ap);
}

void NkLogDebug (const char* fmt, ...)
{
    // Log the message
    va_list ap;
    va_start (ap, fmt);
    NkLogMessage (fmt, NK_LOGLEVEL_DEBUG, ap);
    va_end (ap);
}
