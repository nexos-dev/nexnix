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

#include "cmds/shellcmds.h"
#include "conf/conf.h"
#include <assert.h>
#include <libnex/array.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Terminal shell is run on
static NbObject_t* shellTerm = NULL;

// Variables
static Array_t* shellVars = NULL;

// Variable type
typedef struct _shellvar
{
    StringRef_t* name;
    StringRef_t* val;    // Value of variable
} ShellVar_t;

#define SHELLVAR_GROW_SIZE 64
#define SHELLVAR_MAX_SIZE  16384

#define ARG_ARRAY_GROW_SIZE 16
#define ARG_ARRAY_MAX_SIZE  512

#define SHELL_EXIT 255

// Terminal helpers

// Writes a character to terminal
void NbShellWriteChar (char c)
{
    NbObjCallSvc (shellTerm, NB_TERMINAL_WRITECHAR, &c);
}

// Reads from terminal
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

// Writes to terminal
void NbShellWrite (const char* fmt, ...)
{
    if (shellTerm)
    {
        va_list ap;
        va_start (ap, fmt);
        // Parse format string
        char buf[512] = {0};
        vsnprintf (buf, 512, fmt, ap);
        va_end (ap);
        NbObjCallSvc (shellTerm, NB_TERMINAL_WRITE, buf);
    }
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

// Printf for shell

// These are internal libc structures, we probably shouldn't do this
// But who cares

// Contains printf out data
typedef struct _printfOut
{
    int (*out) (struct _printfOut*, char);    // Function to be called
    char* buf;                                // Buffer being written to
    size_t bufSize;                           // Size of buffer
    size_t bufPos;                            // Current position in buffer
    int charsPrinted;                         // Number of characters printed
} _printfOut_t;

int vprintfCore (_printfOut_t* outData, const char* fmt, va_list ap);

// Disables echoing
static void nbDisableEcho()
{
    NbTerminal_t term;
    NbObjCallSvc (shellTerm, NB_TERMINAL_GETOPTS, &term);
    term.echo = false;
    NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
}

// Enables echoing
static void nbEnableEcho()
{
    NbTerminal_t term;
    NbObjCallSvc (shellTerm, NB_TERMINAL_GETOPTS, &term);
    term.echo = true;
    NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
}

// Printf out function NbShellWritePaged
static int pagedOut (_printfOut_t* out, char c)
{
    if (shellTerm)
    {
        // Write it
        NbShellWriteChar (c);
        ++out->charsPrinted;
        // If we are on last row, prompt to stop
        NbTerminal_t term;
        NbObjCallSvc (shellTerm, NB_TERMINAL_GETOPTS, &term);
        if (term.row == (term.numRows - 1) && c == '\n')
        {
            // We are on last row, prompt to continue
            NbShellWrite ("Press a key to continue...");
            nbDisableEcho();
            char c = 0;
            NbObjCallSvc (shellTerm, NB_TERMINAL_READCHAR, &c);
            nbEnableEcho();
            // Overwrite prompt
            term.col = 0;
            NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
            NbShellWrite ("                          ");
            // Reset column back to zero again
            term.col = 0;
            NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
        }
    }
    return 0;
}

// Printf function for paged output
void NbShellWritePaged (const char* fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    _printfOut_t out = {0};
    out.out = pagedOut;
    vprintfCore (&out, fmt, ap);
    va_end (ap);
}

// Execution functions

// Find by functions for variable
static bool nbShellVarFindBy (const void* data, const void* hint)
{
    const ShellVar_t* var = data;
    if (!strcmp (StrRefGet (var->name), hint))
        return true;
    return false;
}

// Destroy function for variable
static void nbShellVarDestroy (void* data)
{
    ShellVar_t* var = data;
    StrRefDestroy (var->name);
    StrRefDestroy (var->val);
}

// Set a variable to value
bool NbShellSetVar (StringRef_t* var, StringRef_t* val)
{
    // Attempt to find existing variable to overwrite
    size_t pos = ArrayFindElement (shellVars, StrRefGet (var));
    if (pos != ARRAY_ERROR)
    {
        // Overwrite
        ShellVar_t* varSt = ArrayGetElement (shellVars, pos);
        varSt->val = StrRefNew (val);
    }
    else
    {
        // Create new variable
        pos = ArrayFindFreeElement (shellVars);
        if (pos == ARRAY_ERROR)
            return false;
        ShellVar_t* varSt = ArrayGetElement (shellVars, pos);
        varSt->name = StrRefNew (var);
        varSt->val = StrRefNew (val);
    }
    // Special variable cases
    if (!strcmp (StrRefGet (var), "root"))
    {
        // Set cwd to ""
        StringRef_t* empty = StrRefCreate ("");
        StrRefNoFree (empty);
        StringRef_t* var = StrRefCreate ("cwd");
        StrRefNoFree (var);
        return NbShellSetVar (var, empty);
    }
    return true;
}

// Gets value of variable
StringRef_t* NbShellGetVar (const char* varName)
{
    // Attempt to find it
    size_t pos = ArrayFindElement (shellVars, varName);
    if (pos == ARRAY_ERROR)
        return NULL;    // Doesn't exist
    ShellVar_t* var = ArrayGetElement (shellVars, pos);
    if (!var)
        return NULL;
    return var->val;
}

// Gets root filesystem
NbObject_t* NbShellGetRootFs()
{
    // Get variable
    StringRef_t* varRef = NbShellGetVar ("root");
    if (!varRef)
        return NULL;
    // Get full name
    char buf[256];
    snprintf (buf, 256, "/Interfaces/FileSys/%s", StrRefGet (varRef));
    return NbObjFind (buf);
}

// Gets working directory
StringRef_t* NbShellGetWorkDir()
{
    // Get variable
    return NbShellGetVar ("cwd");
}

// Executes a command
bool NbShellExecuteCmd (StringRef_t* cmd, Array_t* args)
{
    // Iterate through command array
    size_t numCmds = ARRAY_SIZE (shellCmdTab);
    for (int i = 0; i < numCmds; ++i)
    {
        // Find a match
        if (!strcmp (shellCmdTab[i].name, StrRefGet (cmd)))
        {
            // Match found, execute command
            return shellCmdTab[i].entry (args);
        }
    }
    NbShellWrite ("nexboot: command \"%s\" not implemented\n", StrRefGet (cmd));
    return false;
}

// argument array destroy function
static void nbShellArgDestroy (void* p)
{
    StringRef_t** ref = p;
    StrRefDestroy (*ref);
}

// Executes a block list
int NbShellExecute (ListHead_t* blocks)
{
    // Iterate through every block
    ListEntry_t* iter = ListFront (blocks);
    while (iter)
    {
        ConfBlock_t* block = ListEntryData (iter);
        // Figure out what to do with block
        if (block->type == CONF_BLOCK_MENUENTRY)
        {
            ConfBlockMenu_t* menu = ListEntryData (iter);
            NbMenuAddEntry (menu->name, menu->blocks);
        }
        else if (block->type == CONF_BLOCK_VARSET)
        {
            ConfBlockSet_t* varSet = ListEntryData (iter);
            StringRef_t* val = NULL;
            // Resolve value
            if (varSet->val.type == CONF_STRING_LITERAL)
                val = varSet->val.literal;
            else if (varSet->val.type == CONF_STRING_VAR)
            {
                // Retrieve variable
                val = NbShellGetVar (StrRefGet (varSet->val.var));
                if (!val)
                {
                    // Error
                    NbShellWrite ("nexboot: Variable \"%s\" doesn't exist\n",
                                  StrRefGet (varSet->val.var));
                    goto iterate;
                }
            }
            // Set variable
            if (!NbShellSetVar (varSet->var, val))
            {
                // Error
                NbShellWrite ("nexboot: Variable array full\n", StrRefGet (varSet->val.var));
                return false;
            }
        }
        else if (block->type == CONF_BLOCK_CMD)
        {
            ConfBlockCmd_t* cmd = ListEntryData (iter);
            // Get command string
            StringRef_t* cmdName = NULL;
            if (cmd->cmd.type == CONF_STRING_LITERAL)
                cmdName = cmd->cmd.literal;
            else if (cmd->cmd.type == CONF_STRING_VAR)
            {
                cmdName = NbShellGetVar (StrRefGet (cmd->cmd.var));
                if (!cmdName)
                {
                    // Error
                    NbShellWrite ("nexboot: Variable \"%s\" doesn't exist\n", StrRefGet (cmdName));
                    goto iterate;
                }
            }
            else
                assert (0);
            // Parse each argument. Create array first
            Array_t* args =
                ArrayCreate (ARG_ARRAY_GROW_SIZE, ARG_ARRAY_MAX_SIZE, sizeof (StringRef_t*));
            ArraySetDestroy (args, nbShellArgDestroy);
            ListEntry_t* argIter = ListFront (cmd->args);
            while (argIter)
            {
                // Grab argument
                ConfBlockCmdArg_t* arg = ListEntryData (argIter);
                assert (arg->hdr.type == CONF_BLOCK_CMDARG);
                size_t pos = ArrayFindFreeElement (args);
                if (pos == ARRAY_ERROR)
                {
                    // Error
                    NbShellWrite ("nexboot: Too many arguments to command \"%s\"\n",
                                  StrRefGet (cmdName));
                    goto iterate;
                }
                // Set pointer. First we need to determine if this is a literal or
                // variable
                StringRef_t** refPtr = ArrayGetElement (args, pos);
                if (arg->str.type == CONF_STRING_LITERAL)
                    *refPtr = StrRefNew (arg->str.literal);
                else if (arg->str.type == CONF_STRING_VAR)
                {
                    *refPtr = NbShellGetVar (StrRefGet (arg->str.var));
                    if (!(*refPtr))
                    {
                        // Error
                        NbShellWrite ("nexboot: Variable \"%s\" doesn't exist\n",
                                      StrRefGet (arg->str.var));
                        // Place an empty string in
                        *refPtr = StrRefCreate ("");
                        StrRefNoFree (*refPtr);
                    }
                    else
                        StrRefNew (*refPtr);
                }
                argIter = ListIterate (argIter);
            }
            // Execute command
            // Special case first: exit
            if (!strcmp (StrRefGet (cmdName), "exit"))
            {
                ArrayDestroy (args);
                return SHELL_EXIT;    // Exit out of shell
            }
            else
            {
                // If fail is set, fail on failure
                bool res = NbShellExecuteCmd (cmdName, args);
                StringRef_t* fail = NbShellGetVar ("fail");
                if (fail && !strcmp (StrRefGet (fail), "1"))
                {
                    // Check for failute
                    if (!res)
                        return false;
                }
            }
            ArrayDestroy (args);
        }
        else
            assert (0);
    iterate:
        iter = ListIterate (iter);
    }
    return true;
}

// File helpers
// These function take into account the working directory

// Gets full directory, including working directory
StringRef_t* NbShellGetFullPath (const char* dir)
{
    StringRef_t* cwd = NbShellGetVar ("cwd");
    StringRef_t* fullDir = NULL;
    if (cwd)
    {
        size_t wdLen = strlen (StrRefGet (cwd));
        fullDir = StrRefCreate (malloc (strlen (dir) + wdLen + 2));
        char* s = StrRefGet (fullDir);
        strcpy (s, StrRefGet (cwd));
        // Check if we need to add a slash
        if (s[wdLen - 1] != '/')
            strcat (s, "/");
        strcat (s, dir);
    }
    else
    {
        fullDir = StrRefCreate (dir);
        StrRefNoFree (fullDir);
    }
    return fullDir;
}

// Opens a file
NbFile_t* NbShellOpenFile (NbObject_t* fs, const char* name)
{
    const char* path = NULL;
    if (*name != '/')
        path = StrRefGet (NbShellGetFullPath (name));
    else
        path = name;
    return NbVfsOpenFile (fs, path);
}

// Gets file info
bool NbShellGetFileInfo (NbObject_t* fs, const char* name, NbFileInfo_t* out)
{
    StringRef_t* fullDir = NbShellGetFullPath (name);
    strcpy (out->name, StrRefGet (fullDir));
    bool res = NbVfsGetFileInfo (fs, out);
    StrRefDestroy (fullDir);
    return res;
}

// Prompting functions

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
    // If there is no terminal, crash
    if (!shellTerm)
        NbCrash();
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
        StrRefNoFree (ctx.line);
        ctx.readCallback = nbShellPrompt2;
        ctx.bufSz = bytesRead;
        ctx.maxBufSz = KEYBUFSZ;
        // Parse it
        ListHead_t* blocks = NbConfParse (&ctx);
        if (!blocks)
            continue;    // Just re-prompt
        // Execute it
        int res = NbShellExecute (blocks);
        if (res == SHELL_EXIT)
            break;
        ListDestroy (blocks);
    }
}

// Main shell routine
bool NbShellLaunch (NbFile_t* confFile)
{
    // Grab terminal
    shellTerm = nbFindPrimaryTerm();
    if (shellTerm)
    {
        // Ensure backspaces aren't echoed
        NbTerminal_t term = {0};
        NbObjCallSvc (shellTerm, NB_TERMINAL_GETOPTS, &term);
        term.echo = true;
        term.echoc = TERM_NO_ECHO_BACKSPACE;
        NbObjCallSvc (shellTerm, NB_TERMINAL_SETOPTS, &term);
    }
    // Initialize context
    shellVars = ArrayCreate (SHELLVAR_GROW_SIZE, SHELLVAR_MAX_SIZE, sizeof (ShellVar_t));
    ArraySetFindBy (shellVars, nbShellVarFindBy);
    ArraySetDestroy (shellVars, nbShellVarDestroy);
    // Welcome
    NbShellWrite ("Welcome to nexboot!\n\n");
    if (!confFile)
    {
        NbShellWrite ("nexboot: no nexboot.cfg found\n");
        nbShellLoop();
    }
    else
    {
        // Start up parser
        ConfContext_t ctx = {0};
        ctx.isFile = true;
        ctx.confFile = confFile;
        ListHead_t* blocks = NbConfParse (&ctx);
        // Launch the shell loop if an error occured
        if (!blocks)
            nbShellLoop();
        // Execute the blocks parsed
        if (!NbShellExecute (blocks))
            NbShellWrite ("nexboot: configuration script returned error\n");
        ListDestroy (blocks);
        // Now launch shell loop
        nbShellLoop();
    }
    ArrayDestroy (shellVars);
    return false;
}
