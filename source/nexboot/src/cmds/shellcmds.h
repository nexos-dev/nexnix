/*
    shellcmds.h - contains table of shell commands
    Copyright 2023 - 2024 The NexNix Project

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

#ifndef _SHELLCMDS_H
#define _SHELLCMDS_H

#include <libnex/array.h>
#include <stdbool.h>

// Entry points to commands
bool NbEchoMain (Array_t*);
bool NbCdMain (Array_t*);
bool NbPwdMain (Array_t*);
bool NbFindMain (Array_t*);
bool NbReadMain (Array_t*);
bool NbObjFindMain (Array_t*);
bool NbLsObjMain (Array_t*);
bool NbObjDumpMain (Array_t*);
bool NbSysinfoMain (Array_t*);
bool NbMmDumpMain (Array_t*);
bool NbMmapDumpMain (Array_t*);
bool NbMountMain (Array_t*);
bool NbUnmountMain (Array_t*);
bool NbLsMain (Array_t*);
bool NbMenuInitUi (Array_t*);
bool NbBootTypeMain (Array_t*);
bool NbPayloadMain (Array_t*);
bool NbBootArgsMain (Array_t*);
bool NbBootModMain (Array_t*);
bool NbBootMain (Array_t*);
bool NbGfxModeMain (Array_t*);

typedef bool (*NbCmdMain) (Array_t*);

typedef struct _shellcmd
{
    const char* name;    // Name of command
    NbCmdMain entry;     // Entry point
} ShellCmd_t;

ShellCmd_t shellCmdTab[] = {
    {.name = "echo",     .entry = NbEchoMain    },
    {.name = "cd",       .entry = NbCdMain      },
    {.name = "pwd",      .entry = NbPwdMain     },
    {.name = "find",     .entry = NbFindMain    },
    {.name = "read",     .entry = NbReadMain    },
    {.name = "objfind",  .entry = NbObjFindMain },
    {.name = "lsobj",    .entry = NbLsObjMain   },
    {.name = "objdump",  .entry = NbObjDumpMain },
    {.name = "sysinfo",  .entry = NbSysinfoMain },
    {.name = "mmdump",   .entry = NbMmDumpMain  },
    {.name = "mmapdump", .entry = NbMmapDumpMain},
    {.name = "mount",    .entry = NbMountMain   },
    {.name = "unmount",  .entry = NbUnmountMain },
    {.name = "ls",       .entry = NbLsMain      },
    {.name = "showui",   .entry = NbMenuInitUi  },
    {.name = "boottype", .entry = NbBootTypeMain},
    {.name = "payload",  .entry = NbPayloadMain },
    {.name = "bootargs", .entry = NbBootArgsMain},
    {.name = "bootmod",  .entry = NbBootModMain },
    {.name = "boot",     .entry = NbBootMain    },
    {.name = "gfxmode",  .entry = NbGfxModeMain }
};

#endif
