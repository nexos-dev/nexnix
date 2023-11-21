/*
    shell.h - contains shell functions
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

#ifndef _SHELL_H
#define _SHELL_H

#include <libnex/array.h>
#include <libnex/stringref.h>
#include <nexboot/nexboot.h>

// Writes to shell terminal
void NbShellWrite (const char* fmt, ...);

// Writes to shell with pagenated output
void NbShellWritePaged (const char* fmt, ...);

// Writes a character to terminal
void NbShellWriteChar (char c);

// Reads from shell terminal
uint32_t NbShellRead (char* buf, uint32_t bufSz);

// Set a variable to value
bool NbShellSetVar (StringRef_t* var, StringRef_t* val);

// Gets value of variable
StringRef_t* NbShellGetVar (const char* varName);

// Gets root filesystem
NbObject_t* NbShellGetRootFs();

// Gets working directory
StringRef_t* NbShellGetWorkDir();

// Gets full directory, including working directory
StringRef_t* NbShellGetFullPath (const char* dir);

// Executes a command
bool NbShellExecuteCmd (StringRef_t* cmd, Array_t* args);

// Executes a block list
bool NbShellExecute (ListHead_t* blocks);

// Opens a file
NbFile_t* NbShellOpenFile (NbObject_t* fs, const char* name);

// Gets file info
bool NbShellGetFileInfo (NbObject_t* fs, const char* name, NbFileInfo_t* out);

#endif
