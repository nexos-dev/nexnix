/*
    unistd.h - contains miscelanious Unix stuff
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

#ifndef _UNISTD_H
#define _UNISTD_H

#include <bits/arch.h>
typedef _saddr_t intptr_t;

#define __NEED_PIDT
#include <bits/types.h>

int execl (const char*, const char*, ...);
int execle (const char*, const char*, ...);
int execlp (const char*, const char*, ...);
int execv (const char*, char* const[]);
int execve (const char*, char* const[], char* const[]);
int execvp (const char*, char* const[]);

pid_t fork();
pid_t getpid();

#endif
