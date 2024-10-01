/*
    backends.h - contains memory object backends
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

#ifndef _BACKEND_H
#define _BACKEND_H

#include <nexke/mm.h>
#include <stdbool.h>

// Kernel backend functions
bool KvmInitObj (MmObject_t* obj);
bool KvmDestroyObj (MmObject_t* obj);
bool KvmPageIn (MmObject_t* obj, size_t offset, MmPage_t* page);
bool KvmPageOut (MmObject_t* obj, size_t offset);

static void* kvmBackend[] = {KvmPageIn, KvmPageOut, KvmInitObj, KvmDestroyObj};

static void* backends[] = {NULL, kvmBackend};

#endif
