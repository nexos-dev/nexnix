// trap.S - contains trap handler code
// Copyright 2024 The NexNix Project
//
// Licensed under the Apache License, Version 2.0 (the "License")
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

.text

// Vector numbers
#define CPU_EXEC_SP_EL0     0
#define CPU_EXEC_SP_ELX     4
#define CPU_EXEC_LOWER_EL   8
#define CPU_EXEC_LOWER_EL32 12

#define CPU_IRQ_SP_EL0     1
#define CPU_IRQ_SP_ELX     5
#define CPU_IRQ_LOWER_EL   9
#define CPU_IRQ_LOWER_EL32 13

#define CPU_FIQ_SP_EL0     2
#define CPU_FIQ_SP_ELX     6
#define CPU_FIQ_LOWER_EL   10
#define CPU_FIQ_LOWER_EL32 14

#define CPU_SERR_SP_EL0     3
#define CPU_SERR_SP_ELX     7
#define CPU_SERR_LOWER_EL   11
#define CPU_SERR_LOWER_EL32 15

CpuVectorExit:
    add sp, sp, 40      // Skip over stuff we don't need to set
    ldp x0, x30, [sp], #16
    msr SP_EL0, x0
    ldp x29, x28, [sp], #16
    ldp x27, x26, [sp], #16
    ldp x25, x24, [sp], #16
    ldp x23, x22, [sp], #16
    ldp x21, x20, [sp], #16
    ldp x19, x18, [sp], #16
    ldp x17, x16, [sp], #16
    ldp x15, x14, [sp], #16
    ldp x13, x12, [sp], #16
    ldp x11, x10, [sp], #16
    ldp x9, x8, [sp], #16
    ldp x7, x6, [sp], #16
    ldp x5, x4, [sp], #16
    ldp x3, x2, [sp], #16
    ldp x1, x0, [sp], #16
    eret            // Back to EL0

.extern PltTrapDispatch

// Standard handler macro
.macro CpuGenVector i
CpuVector\i:
    // Handler has max size of 128 bytes
    // Save all the registers
    stp x1, x0, [sp, #-16]!
    stp x3, x2, [sp, #-16]!
    stp x5, x5, [sp, #-16]!
    stp x7, x6, [sp, #-16]!
    stp x9, x8, [sp, #-16]!
    stp x11, x10, [sp, #-16]!
    stp x13, x12, [sp, #-16]!
    stp x15, x14, [sp, #-16]!
    stp x17, x16, [sp, #-16]!
    stp x19, x18, [sp, #-16]!
    stp x21, x20, [sp, #-16]!
    stp x23, x22, [sp, #-16]!
    stp x25, x24, [sp, #-16]!
    stp x27, x26, [sp, #-16]!
    stp x29, x28, [sp, #-16]!
    mrs x0, SP_EL0
    stp x0, x30, [sp, #-16]!
    // Store handler number and and ELR
    mov x0, #\i
    mrs x1, ELR_EL1
    stp x1, x0, [sp, #-16]!
    // Store SPSR and TPIDR
    mrs x0, SPSR_EL1
    mrs x1, TPIDR_EL0
    stp x1, x0, [sp, #-16]!
    mov x0, 0
    str x0, [sp, #-8]!
    // Call PltTrapDispatch
    mov x0, sp
    bl PltTrapDispatch
    // Call common exit code
    b CpuVectorExit
.endm

// Table that gets placed in VBAR
.balign 0x800
.global CpuVectorTable
CpuVectorTable:
    CpuGenVector CPU_EXEC_SP_EL0
.balign 0x80
    CpuGenVector CPU_IRQ_SP_EL0
.balign 0x80
    CpuGenVector CPU_FIQ_SP_EL0
.balign 0x80
    CpuGenVector CPU_SERR_SP_EL0
.balign 0x80
    CpuGenVector CPU_EXEC_SP_ELX
.balign 0x80
    CpuGenVector CPU_IRQ_SP_ELX
.balign 0x80
    CpuGenVector CPU_FIQ_SP_ELX
.balign 0x80
    CpuGenVector CPU_SERR_SP_ELX
.balign 0x80
    CpuGenVector CPU_EXEC_LOWER_EL
.balign 0x80
    CpuGenVector CPU_IRQ_LOWER_EL
.balign 0x80
    CpuGenVector CPU_FIQ_LOWER_EL
.balign 0x80
    CpuGenVector CPU_SERR_LOWER_EL
.balign 0x80
    CpuGenVector CPU_EXEC_LOWER_EL32
.balign 0x80
    CpuGenVector CPU_IRQ_LOWER_EL32
.balign 0x80
    CpuGenVector CPU_FIQ_LOWER_EL32
.balign 0x80
    CpuGenVector CPU_SERR_LOWER_EL32
