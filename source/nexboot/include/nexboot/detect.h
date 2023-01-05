/*
    detect.h - contains hardware detection result structure
    Copyright 2022, 2023 The NexNix Project

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

#ifndef _DETECT_H
#define _DETECT_H

#ifndef __ASSEMBLER__
#include <stdint.h>

// CPU detection result
typedef struct _cpuDetect
{
    uint8_t family;      // Family of architecture (e.g., x86)
    uint8_t arch;        // Architecture of system
    uint16_t version;    // Version of CPU (e.g., on i386+, would 386, 486, etc.)
    uint16_t flags;      // Flags of this CPU.
                         // On x86, bit 0 = FPU exists
} __attribute__ ((packed)) DetectCpuInfo_t;

// System tables detected
typedef struct _systabs
{
    uint32_t detected;    // Bit mask of detected tables
    uint32_t tabs[32];    // Detected tables
} __attribute__ ((packed)) DetectSysTabs_t;

#define NBLOAD_TABLE_ACPI    0
#define NBLOAD_TABLE_PNP     1
#define NBLOAD_TABLE_APM     2
#define NBLOAD_TABLE_MPS     3
#define NBLOAD_TABLE_SMBIOS  4
#define NBLOAD_TABLE_SMBIOS3 5
#define NBLOAD_TABLE_BIOS32  6

#endif

// CPU families
#define NBLOAD_CPU_FAMILY_X86 1

// CPU archutectures
#define NBLOAD_CPU_ARCH_I386   1
#define NBLOAD_CPU_ARCH_X86_64 2

// CPU versions
#define NBLOAD_CPU_VERSION_386   1
#define NBLOAD_CPU_VERSION_486   2
#define NBLOAD_CPU_VERSION_CPUID 3    // Use CPUID to detect

// CPU flags
#define NBLOAD_CPU_FLAG_FPU_EXISTS (1 << 0)

#ifndef __ASSEMBLER__
// Main detection structure
typedef struct _detect
{
    uint32_t sig;               // Contains 0xDEADBEEF
    uint16_t logOffset;         // Offset of log
    uint16_t logSeg;            // Segment of log
    uint16_t logSize;           // Size of log
    uint8_t pad1[2];            // Padding
    DetectCpuInfo_t cpu;        // CPU detection results
    DetectSysTabs_t sysTabs;    // System tables
} __attribute__ ((packed)) NbloadDetect_t;
#endif

// Signature of structure
#define NBLOAD_SIGNATURE 0xDEADBEEF

#endif
