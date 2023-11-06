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

#include <assert.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

// The log herein is quite simple. It is themed after syslog, as described
// in RFC 3164. There are a few differences, but the ideas are the same

// Early log portion

// Log structure format
typedef struct _nbLogEnt
{
    int priority;     // Contains facility and severity (but facility is always 0)
    short minute;     // Minute since boot of message
    short second;     // Seconds since boot of message
    short ms;         // Milliseconds since boot of message
    char msg[256];    // Message itself
} nbLogEntryEarly_t;

// Temporary initialization log
static nbLogEntryEarly_t logEntries[64] = {0};
static short curEntry = 0;
static int minSeverity = 0;
static bool logInit = false;
static NbObject_t* logObj = NULL;

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

void NbLogMessageEarly (const char* fmt, int level, ...)
{
    va_list ap;
    va_start (ap, level);
    // snprintf it
    vsnprintf (logEntries[curEntry].msg, 256, fmt, ap);
    // Log it
    logEntries[curEntry].priority = level;
    // Check if we should print it
    if (level <= minSeverity)
        NbPrintEarly (logEntries[curEntry].msg);
    va_end (ap);
    ++curEntry;
}

static bool printEarlyDisabled = false;

void NbDisablePrintEarly()
{
    printEarlyDisabled = true;
}

void NbPrintEarly (const char* s)
{
    if (!printEarlyDisabled)
    {
        while (*s)
        {
            NbFwEarlyPrint (*s);
            ++s;
        }
    }
}

// Assertion failure implementation
void __attribute__ ((noreturn))
__assert_failed (const char* expr, const char* file, int line, const char* func)
{
    if (!logInit)
    {
        // Log the message
        NbLogMessageEarly (
            "Assertion '%s' failed: file %s, line %d, function %s\r\n",
            NEXBOOT_LOGLEVEL_EMERGENCY,
            expr,
            file,
            line,
            func);
    }
    else
    {
        // Use object service
        char buf[256];
        snprintf (buf,
                  256,
                  "Assertion '%s' failed: file %s, line %d, function %s\r\n",
                  expr,
                  file,
                  line,
                  func);
        NbObjCallSvc (logObj, NB_LOG_WRITE, buf);
    }
    NbCrash();
    __builtin_unreachable();
}

// Log variables
static NbloadDetect_t* nbDetect = NULL;

typedef struct _logEntry
{
    const char* msg;    // Message of entry
    int priority;       // Contains facility and severity (but facility is always 0)
    short minute;       // Minute since boot of message
    short second;       // Seconds since boot of message
    short ms;           // Milliseconds since boot of message
    struct _logEntry* next;    // Next entry in list
    struct _logEntry* prev;    // Previous entry in list
} NbLogEntry_t;

typedef struct _log
{
    NbLogEntry_t* entries;        // List of log entries
    NbLogEntry_t* entriesEnd;     // End of entries list
    NbObject_t* outputDevs[8];    // Output devices for each log level
    int logLevel;                 // Current log level
} NbLog_t;

// Main log functions
static bool LogDumpData (void* objp, void* unused)
{
    return true;
}

static bool LogNotify (void* objp, void* unused)
{
    return true;
}

static void logNewEntry (NbLog_t* log, const char* msg, int priority)
{
    NbLogEntry_t* logEntry = (NbLogEntry_t*) malloc (sizeof (NbLogEntry_t));
    assert (logEntry);
    memset (logEntry, 0, sizeof (NbLogEntry_t));
    logEntry->msg = msg;
    logEntry->priority = priority;
    // Add to list
    if (log->entriesEnd)
        log->entriesEnd->next = logEntry;
    else
        log->entries = logEntry;    // If list is empty set entries pointer
    logEntry->prev = log->entriesEnd;
    logEntry->next = NULL;
    log->entriesEnd = logEntry;
}

static bool LogWrite (void* objp, void* strp)
{
    assert (objp && strp);
    NbObject_t* logObj = objp;
    NbLogStr_t* str = strp;
    NbLog_t* log = logObj->data;
    // Create new entry
    logNewEntry (log, str->str, str->priority);
    // Determine wheter to print it
    if (str->priority <= log->logLevel)
    {
        NbTerminal_t* term = log->outputDevs[str->priority - 1]->data;
        // If this is the primary display, clear the screen
        if (term->isPrimary)
        {
            NbObjCallSvc (log->outputDevs[str->priority - 1],
                          NB_TERMINAL_CLEAR,
                          NULL);
        }
        NbObjCallSvc (log->outputDevs[str->priority - 1],
                      NB_TERMINAL_WRITE,
                      (void*) str->str);
    }
    return true;
}

extern NbObjSvcTab_t logSvcTab;

static int levelToPriority[] = {0,
                                NEXBOOT_LOGLEVEL_ERROR,
                                NEXBOOT_LOGLEVEL_WARNING,
                                NEXBOOT_LOGLEVEL_INFO,
                                NEXBOOT_LOGLEVEL_DEBUG};

static bool LogObjInit (void* objp, void* unused)
{
    NbObject_t* obj = objp;
    NbLog_t* log = malloc (sizeof (NbLog_t));
    memset (log, 0, sizeof (NbLog_t));
    NbObjSetData (obj, log);
    log->logLevel = levelToPriority[NEXNIX_LOGLEVEL];
    // Copy old logs
#ifdef NEXNIX_FW_BIOS
    assert (nbDetect);
    uint16_t* oldLog = (uint16_t*) ((nbDetect->logSeg * 0x10) + nbDetect->logOffset);
    for (int i = 0; i < nbDetect->logSize; i += 6)
    {
        uint16_t msgOff = *oldLog;
        uint16_t msgSeg = *(oldLog + 1);
        uint32_t msgAddr = (msgSeg * 0x10) + msgOff;
        uint16_t msgLevel = *(oldLog + 2);
        if (!msgAddr)    // Last entry is zeroed
            break;
        int priority = levelToPriority[msgLevel];
        logNewEntry (log, (char*) msgAddr, priority);
        oldLog += 3;
    }
#endif
    for (int i = 0; i < curEntry; ++i)
    {
        // Copy bootstrap log entries
        logNewEntry (log, logEntries[i].msg, logEntries[i].priority);
    }
    // Find appropriate output devices by enumerating devices
    NbObject_t* iter = NULL;
    NbObject_t* devDir = NbObjFind ("/Devices");
    int numConsoles = 0;
    int numSerialPorts = 1;
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (iter->type == OBJ_TYPE_DEVICE &&
            iter->interface == OBJ_INTERFACE_TERMINAL)
        {
            // Acquire terminal
            NbTerminal_t term;
            NbObjCallSvc (iter, NB_TERMINAL_GETOPTS, &term);
            // Determine output device type
            if (term.outEnd->interface == OBJ_INTERFACE_CONSOLE)
            {
                // Clear the terminal
                // NbObjCallSvc (term.outEnd, NB_CONSOLEHW_CLEAR, NULL);
                ++numConsoles;
                // Determine where this would be best suited
                if (numConsoles == 1)
                {
                    // If this is the first console, set as much as log level allows
                    for (int i = 0; i < minSeverity; ++i)
                        log->outputDevs[i] = iter;
                }
                else if (numConsoles == 2)
                {
                    // Set warning, notice and info levels
                    log->outputDevs[3] = iter;
                    log->outputDevs[4] = iter;
                    log->outputDevs[5] = iter;
                }
                else if (numConsoles == 3)
                {
                    // Set notice and info levels
                    log->outputDevs[4] = iter;
                    log->outputDevs[5] = iter;
                }
            }
            else if (term.outEnd->interface == OBJ_INTERFACE_RS232)
            {
                // If this is the first console, set highest levels only
                if (!log->outputDevs[0])
                    log->outputDevs[0] = iter;
                if (!log->outputDevs[1])
                    log->outputDevs[1] = iter;
                if (!log->outputDevs[2])
                    log->outputDevs[2] = iter;
                if (!log->outputDevs[3])
                    log->outputDevs[3] = iter;
                if (!log->outputDevs[4])
                    log->outputDevs[4] = iter;
                if (!log->outputDevs[5])
                    log->outputDevs[5] = iter;
                if (!log->outputDevs[6])
                    log->outputDevs[6] = iter;
                if (!log->outputDevs[7])
                    log->outputDevs[7] = iter;
            }
        }
    }
    return true;
}

static bool LogSetLevel (void* objp, void* param)
{
    NbObject_t* obj = objp;
    NbLog_t* log = obj->data;
    int level = (int) param;
    if (level <= 7)
        return false;
    log->logLevel = level;
    return true;
}

static NbObjSvc logSvcs[] =
    {LogObjInit, NULL, NULL, LogDumpData, LogNotify, LogWrite, LogSetLevel};

NbObjSvcTab_t logSvcTab = {ARRAY_SIZE (logSvcs), logSvcs};

void NbLogInit2 (NbloadDetect_t* detect)
{
    nbDetect = detect;
    // Create object
    NbObjCreate ("/Interfaces", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
    logObj = NbObjCreate ("/Interfaces/SysLog", OBJ_TYPE_LOG, OBJ_INTERFACE_LOG);
    assert (logObj);
    NbObjInstallSvcs (logObj, &logSvcTab);
    NbObjRef (logObj);
    // Disable BIOS printing
    NbDisablePrintEarly();
}

// Standard logging function
void NbLogMessage (const char* fmt, int level, ...)
{
    va_list ap;
    va_start (ap, level);
    // Format it
    char buf[256] = {0};
    vsnprintf (buf, 256, fmt, ap);
    NbLogStr_t str = {0};
    str.priority = level;
    str.str = buf;
    // Log it
    NbObjCallSvc (logObj, NB_LOG_WRITE, &str);
    va_end (ap);
}
