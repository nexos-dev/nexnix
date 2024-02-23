/*
    cpuid.c - contains CPUID feature detection
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

#include <nexke/cpu.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <stdint.h>
#include <string.h>

typedef struct _cpuidInfo
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} CpuCpuid_t;

// Raw feature bits
// 01h EDX
#define CPUID_FEATURE_FPU       (1 << 0)
#define CPUID_FEATURE_VME       (1 << 1)
#define CPUID_FEATURE_DE        (1 << 2)
#define CPUID_FEATURE_PSE       (1 << 3)
#define CPUID_FEATURE_TSC       (1 << 4)
#define CPUID_FEATURE_MSR       (1 << 5)
#define CPUID_FEATURE_PAE       (1 << 6)
#define CPUID_FEATURE_MCE       (1 << 7)
#define CPUID_FEATURE_CMPXCHG8B (1 << 8)
#define CPUID_FEATURE_APIC      (1 << 9)
#define CPUID_FEATURE_SYSENTER  (1 << 11)
#define CPUID_FEATURE_MTRR      (1 << 12)
#define CPUID_FEATURE_PGE       (1 << 13)
#define CPUID_FEATURE_MCA       (1 << 14)
#define CPUID_FEATURE_CMOV      (1 << 15)
#define CPUID_FEATURE_PAT       (1 << 16)
#define CPUID_FEATURE_PSE36     (1 << 17)
#define CPUID_FEATURE_CLFLUSH   (1 << 19)
#define CPUID_FEATURE_MMX       (1 << 23)
#define CPUID_FEATURE_FXSR      (1 << 24)
#define CPUID_FEATURE_SSE       (1 << 25)
#define CPUID_FEATURE_SSE2      (1 << 26)
#define CPUID_FEATURE_HT        (1 << 28)

// 01h ECX
#define CPUID_FEATURE_SSE3         (1 << 0)
#define CPUID_FEATURE_MONITOR      (1 << 3)
#define CPUID_FEATURE_VMX          (1 << 5)
#define CPUID_FEATURE_SSSE3        (1 << 9)
#define CPUID_FEATURE_CMPXCHG16B   (1 << 13)
#define CPUID_FEATURE_PCID         (1 << 17)
#define CPUID_FEATURE_SSE41        (1 << 19)
#define CPUID_FEATURE_SSE42        (1 << 20)
#define CPUID_FEATURE_X2APIC       (1 << 21)
#define CPUID_FEATURE_POPCNT       (1 << 23)
#define CPUID_FEATURE_TSC_DEADLINE (1 << 24)
#define CPUID_FEATURE_XSAVE        (1 << 26)
#define CPUID_FEATURE_OSXSAVE      (1 << 27)
#define CPUID_FEATURE_AVX          (1 << 28)
#define CPUID_FEATURE_RDRAND       (1 << 30)

// 07h EBX
#define CPUID_FEATURE_FSGSBASE (1 << 0)
#define CPUID_FEATURE_SMEP     (1 << 7)
#define CPUID_FEATURE_INVPCID  (1 << 10)

// 80000001h ECX
#define CPUID_FEATURE_LAHF  (1 << 0)
#define CPUID_FEATURE_SVM   (1 << 2)
#define CPUID_FEATURE_SSE4A (1 << 6)
#define CPUID_FEATURE_SSE5  (1 << 11)

// 80000001h EDX
#define CPUID_FEATURE_SYSCALL (1 << 11)
#define CPUID_FEATURE_XD      (1 << 20)
#define CPUID_FEATURE_1GB     (1 << 26)
#define CPUID_FEATURE_RDTSCP  (1 << 27)
#define CPUID_FEATURE_LM      (1 << 29)

// Globals
static size_t maxEax = 0;
static size_t maxExtEax = 0;

// Executes cpuid instruction
static void cpuCpuid (uint32_t code, uint32_t extCode, CpuCpuid_t* cpuid)
{
    asm volatile("cpuid"
                 : "=a"(cpuid->eax), "=b"(cpuid->ebx), "=c"(cpuid->ecx), "=d"(cpuid->edx)
                 : "a"(code), "c"(extCode));
}

static void cpuidSetType (NkCcb_t* ccb)
{
    CpuCpuid_t cpuid;
    cpuCpuid (1, 0, &cpuid);
    ccb->archCcb.stepping = cpuid.eax & 0xF;
    // Set family
    ccb->archCcb.family = ((cpuid.eax >> 8) & 0xF) + ((cpuid.eax >> 20) & 0xFF);
    // Set model
    ccb->archCcb.model = ((cpuid.eax >> 4) & 0xF) | (((cpuid.eax >> 16) & 0xF) << 4);
}

static void cpuidSetFeatures (NkCcb_t* ccb)
{
    // Call 01h
    CpuCpuid_t cpuid;
    cpuCpuid (1, 0, &cpuid);
    uint32_t edx = cpuid.edx;
    // Set features
    NkArchCcb_t* archCcb = &ccb->archCcb;
    archCcb->features = 0;
    if (edx & CPUID_FEATURE_FPU)
        archCcb->features |= CPU_FEATURE_FPU;
    if (edx & CPUID_FEATURE_VME)
        archCcb->features |= CPU_FEATURE_VME;
    if (edx & CPUID_FEATURE_DE)
        archCcb->features |= CPU_FEATURE_DE;
    if (edx & CPUID_FEATURE_PSE)
        archCcb->features |= CPU_FEATURE_PSE;
    if (edx & CPUID_FEATURE_TSC)
        archCcb->features |= CPU_FEATURE_TSC;
    if (edx & CPUID_FEATURE_MSR)
        archCcb->features |= CPU_FEATURE_MSR;
    if (edx & CPUID_FEATURE_PAE)
        archCcb->features |= CPU_FEATURE_PAE;
    if (edx & CPUID_FEATURE_MCE)
        archCcb->features |= CPU_FEATURE_MCE;
    if (edx & CPUID_FEATURE_CMPXCHG8B)
        archCcb->features |= CPU_FEATURE_CMPXCHG8B;
    if (edx & CPUID_FEATURE_APIC)
        archCcb->features |= CPU_FEATURE_APIC;
    if (edx & CPUID_FEATURE_MTRR)
        archCcb->features |= CPU_FEATURE_MTRR;
    if (edx & CPUID_FEATURE_SYSENTER)
        archCcb->features |= CPU_FEATURE_SYSENTER;
    if (edx & CPUID_FEATURE_MCA)
        archCcb->features |= CPU_FEATURE_MCA;
    if (edx & CPUID_FEATURE_PGE)
        archCcb->features |= CPU_FEATURE_PGE;
    if (edx & CPUID_FEATURE_CMOV)
        archCcb->features |= CPU_FEATURE_CMOV;
    if (edx & CPUID_FEATURE_PAT)
        archCcb->features |= CPU_FEATURE_PAT;
    if (edx & CPUID_FEATURE_PSE36)
        archCcb->features |= CPU_FEATURE_PSE36;
    if (edx & CPUID_FEATURE_CLFLUSH)
        archCcb->features |= CPU_FEATURE_CLFLUSH;
    if (edx & CPUID_FEATURE_MMX)
        archCcb->features |= CPU_FEATURE_MMX;
    if (edx & CPUID_FEATURE_FXSR)
        archCcb->features |= CPU_FEATURE_FXSR;
    if (edx & CPUID_FEATURE_SSE)
        archCcb->features |= CPU_FEATURE_SSE;
    if (edx & CPUID_FEATURE_SSE2)
        archCcb->features |= CPU_FEATURE_SSE2;
    if (edx & CPUID_FEATURE_HT)
        archCcb->features |= CPU_FEATURE_HT;
    // Set ECX features
    uint32_t ecx = cpuid.ecx;
    if (ecx & CPUID_FEATURE_SSE3)
        archCcb->features |= CPU_FEATURE_SSE3;
    if (ecx & CPUID_FEATURE_MONITOR)
        archCcb->features |= CPU_FEATURE_MONITOR;
    if (ecx & CPUID_FEATURE_VMX)
        archCcb->features |= CPU_FEATURE_VMX;
    if (ecx & CPUID_FEATURE_SSSE3)
        archCcb->features |= CPU_FEATURE_SSSE3;
    if (ecx & CPUID_FEATURE_CMPXCHG16B)
        archCcb->features |= CPU_FEATURE_CMPXCHG16B;
    if (ecx & CPUID_FEATURE_PCID)
        archCcb->features |= CPU_FEATURE_PCID;
    if (ecx & CPUID_FEATURE_SSE41)
        archCcb->features |= CPU_FEATURE_SSE41;
    if (ecx & CPUID_FEATURE_SSE42)
        archCcb->features |= CPU_FEATURE_SSE42;
    if (ecx & CPUID_FEATURE_X2APIC)
        archCcb->features |= CPU_FEATURE_X2APIC;
    if (ecx & CPUID_FEATURE_POPCNT)
        archCcb->features |= CPU_FEATURE_POPCNT;
    if (ecx & CPUID_FEATURE_TSC_DEADLINE)
        archCcb->features |= CPU_FEATURE_TSC_DEADLINE;
    if (ecx & CPUID_FEATURE_XSAVE)
        archCcb->features |= CPU_FEATURE_XSAVE;
    if (ecx & CPUID_FEATURE_OSXSAVE)
        archCcb->features |= CPU_FEATURE_OSXSAVE;
    if (ecx & CPUID_FEATURE_AVX)
        archCcb->features |= CPU_FEATURE_AVX;
    if (ecx & CPUID_FEATURE_RDRAND)
        archCcb->features |= CPU_FEATURE_RDRAND;
    // Call 07h
    if (maxEax >= 7)
    {
        cpuCpuid (7, 0, &cpuid);
        uint32_t ebx = cpuid.ebx;
        if (ebx & CPUID_FEATURE_FSGSBASE)
            archCcb->features |= CPU_FEATURE_FSGSBASE;
        if (ebx & CPUID_FEATURE_SMEP)
            archCcb->features |= CPU_FEATURE_SMEP;
        if (ebx & CPUID_FEATURE_INVPCID)
            archCcb->features |= CPU_FEATURE_INVPCID;
    }
    // Call 80000001h
    if (maxExtEax >= 0x80000001)
    {
        cpuCpuid (0x80000001, 0, &cpuid);
        uint32_t ecx = cpuid.ecx;
        if (ecx & CPUID_FEATURE_LAHF)
            archCcb->features |= CPU_FEATURE_LAHF;
        if (ecx & CPUID_FEATURE_SVM)
            archCcb->features |= CPU_FEATURE_SVM;
        if (ecx & CPUID_FEATURE_SSE4A)
            archCcb->features |= CPU_FEATURE_SSE4A;
        if (ecx & CPUID_FEATURE_SSE5)
            archCcb->features |= CPU_FEATURE_SSE5;
        uint32_t edx = cpuid.edx;
        if (edx & CPUID_FEATURE_1GB)
            archCcb->features |= CPU_FEATURE_1GB;
        if (edx & CPUID_FEATURE_SYSCALL)
            archCcb->features |= CPU_FEATURE_SYSCALL;
        if (edx & CPUID_FEATURE_XD)
            archCcb->features |= CPU_FEATURE_XD;
        if (edx & CPUID_FEATURE_RDTSCP)
            archCcb->features |= CPU_FEATURE_RDTSCP;
        if (edx & CPUID_FEATURE_LM)
            archCcb->features |= CPU_FEATURE_LM;
    }
}

// Determine address sizes through CPUID
static void cpuidSetAddrSz (NkCcb_t* ccb)
{
    CpuCpuid_t cpuid;
    if (maxExtEax >= 0x80000008)
    {
        cpuCpuid (0x80000008, 0, &cpuid);
        ccb->archCcb.physAddrBits = cpuid.eax & 0xFF;
        ccb->archCcb.virtAddrBits = (cpuid.eax >> 8) & 0xFF;
    }
    else
    {
        // Set them to be unspecified
        ccb->archCcb.physAddrBits = 0, ccb->archCcb.virtAddrBits = 0;
    }
}

// Feature string table
static const char* cpuFeatureStrings[] = {
    "FPU",          "VME",       "DE",      "PSE",      "TSC",    "MSR",        "PAE",
    "MCE",          "CMPXCHG8B", "APIC",    "SYSENTER", "MTRR",   "PGE",        "MCA",
    "CMOV",         "PAT",       "PSE36",   "CLFLUSH",  "MMX",    "FXSR",       "SSE",
    "SSE2",         "HT",        "SSE3",    "MONITOR",  "SSSE3",  "CMPXCHG16B", "SSE41",
    "POPCNT",       "LAHF",      "SYSCALL", "XD",       "1GB",    "RDTSCP",     "LM",
    "FSGSBASE",     "SMEP",      "INVPCID", "VMX",      "PCID",   "SSE42",      "X2APIC",
    "TSC_DEADLINE", "XSAVE",     "OSXSAVE", "AVX",      "RDRAND", "SYSENTER64", "SYSCALL64",
    "SVM",          "SSE4A",     "SSE5"};

void CpuDetectCpuid (NkCcb_t* ccb)
{
    CpuCpuid_t cpuid;
    // Determine vendor and max function
    cpuCpuid (0, 0, &cpuid);
    maxEax = cpuid.eax;
    char vendor[13] = {0};
    // Copy vendor string
    memcpy (vendor, &cpuid.ebx, 4);
    memcpy (vendor + 4, &cpuid.edx, 4);
    memcpy (vendor + 8, &cpuid.ecx, 4);
    // Check vendor
    if (!strcmp (vendor, "GenuineIntel"))
        ccb->archCcb.vendor = CPU_VENDOR_INTEL;
    else if (!strcmp (vendor, "AuthenticAMD"))
        ccb->archCcb.vendor = CPU_VENDOR_AMD;
    cpuCpuid (0x80000000, 0, &cpuid);
    maxExtEax = cpuid.eax;
    // Check if they are supported
    if (!(maxExtEax & (1 << 31)))
        maxExtEax = 0;
    // Set feature bits
    cpuidSetFeatures (ccb);
    // Set stepping and family
    cpuidSetType (ccb);
    // Set address sizes
    cpuidSetAddrSz (ccb);
    // Log out supported features
    NkLogInfo ("nexke: detected CPU features: ");
    for (int i = 0; i < 64; ++i)
    {
        if (ccb->archCcb.features & (1ULL << i))
            NkLogInfo ("%s ", cpuFeatureStrings[i]);
    }
    NkLogInfo ("\n");
}
