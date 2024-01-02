// cpu.S - contains CPU asm code
// Copyright 2023s The NexNix Project
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

.global NbCrash

NbCrash:
    wfi
    b NbCrash

.global NbCpuLaunchKernelAsm

NbCpuLaunchKernelAsm:
    mov sp, x2
    // Grab current EL
    mrs x9, CurrentEl
    and x9, x9, #8
    cmp x9, #8         // Compare it with EL2
    b.eq .el2
    // We are in EL1, this is easy
    mov x3, x0
    mov x0, x1
    blr x3
.el2:
    dsb sy
    isb
    // Setup MAIR
    mov x10, #0x44FF
    msr MAIR_EL1, x10
    // We have to set up EL1 state and then switch to it for this to work
    // Ensure timers don't trap
    mrs x9, CNTHCTL_EL2
    orr x9, x9, #3          // Disable traps
    msr CNTHCTL_EL2, x9
    msr CNTVOFF_EL2, xzr
    // Enable AArch64 for EL1
    ldr x9, =0x80000002
    msr HCR_EL2, x9
    // Disable SIMD trapping
    mov x8, #0x33FF
    msr cptr_el2, x8
    msr hstr_el2, xzr
    // Prepare EL1 state
    ldr x9, =0x3c4
    msr ELR_EL2, x0
    msr SPSR_EL2, x9
    mov x0, x1          // Get bootinfo
    eret                // Go to EL1

