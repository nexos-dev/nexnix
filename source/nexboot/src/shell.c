/*
    shell.c - contains shell routines
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

#include "conf/conf.h"
#include <assert.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/nexboot.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Terminal shell is run on
static NbObject_t* shellTerm = NULL;

// Writes to terminal
void NbShellWrite (const char* fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    // Parse format string
    char buf[512] = {0};
    vsnprintf (buf, 512, fmt, ap);
    va_end (ap);
    NbObjCallSvc (shellTerm, NB_TERMINAL_WRITE, buf);
}

uint32_t NbShellRead (char* buf, uint32_t bufSz)
{
    NbTermRead_t read;
    read.buf = buf;
    read.bufSz = bufSz - 2;
    NbObjCallSvc (shellTerm, NB_TERMINAL_READ, &read);
    size_t len = strlen (buf);
    // Add a newline
    buf[len] = '\n';
    buf[len + 1] = '\0';
    return len + 1;    // Account for newline
}

static NbObject_t* nbFindPrimaryTerm()
{
    NbObject_t* iter = NULL;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (NbObjGetInterface (iter) == OBJ_INTERFACE_TERMINAL)
        {
            NbTerminal_t term;
            NbObjCallSvc (iter, NB_TERMINAL_GETOPTS, &term);
            if (term.isPrimary)
                return iter;
        }
    }
    return NULL;
}

static void nbShellPrompt()
{
    NbShellWrite ("nexboot>");
}

// Secondary prompt
static void nbShellPrompt2 (ConfContext_t* ctx)
{
    assert (!ctx->isFile);
    NbShellWrite (">");
    NbShellRead (StrRefGet (ctx->line), ctx->maxBufSz);
}

static void nbShellLoop()
{
#define KEYBUFSZ 256
    char* keyInput = malloc (KEYBUFSZ);
    while (1)
    {
        nbShellPrompt();    // Print prompt
        uint32_t bytesRead = NbShellRead (keyInput, KEYBUFSZ);
        // Start up parser
        ConfContext_t ctx = {0};
        ctx.isFile = false;
        ctx.line = StrRefCreate (keyInput);
        ctx.readCallback = nbShellPrompt2;
        ctx.bufSz = bytesRead;
        ctx.maxBufSz = KEYBUFSZ;
        ListHead_t* blocks = NbConfParse (&ctx);
    }
    free (keyInput);
}

// Main shell routine
bool NbShellLaunch (NbFile_t* confFile)
{
    // Grab terminal
    shellTerm = nbFindPrimaryTerm();
    assert (shellTerm);
    // Welcome
    NbShellWrite ("Welcome to nexboot!\n\n");
    // Ensure backspaces aren't echoed
    NbTerminal_t term = {0};
    term.echo = true;
    term.echoc = TERM_NO_ECHO_BACKSPACE;
    NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
    if (!confFile)
    {
        // Launch shell loop
        nbShellLoop();
    }
    else
    {
        // Start up parser
        ConfContext_t ctx = {0};
        ctx.isFile = true;
        ctx.confFile = confFile;
        ListHead_t* blocks = NbConfParse (&ctx);

        // If an error occurs, then launch shell loop
        if (!blocks)
            nbShellLoop();
    }
    return false;
}
