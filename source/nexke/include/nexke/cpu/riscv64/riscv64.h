/*
    riscv64.h - contains nexke riscv64 stuff
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

#ifndef _RISCV64_H
#define _RISCV64_H

// CPU page size
#define NEXKE_CPU_PAGESZ 0x1000

// PFN map base
#define NEXKE_PFNMAP_BASE 0xFFFFFFFD00000000
#define NEXKE_PFNMAP_MAX 0xF7FFFFFF0

#endif
