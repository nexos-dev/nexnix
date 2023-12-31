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

.section .text
.option norelax
.option pic

.global NbCrash
NbCrash:
    la t0, NbCrash
    jr t0
    

.global NbCpuLaunchKernelAsm

#define NB_KE_STACK_BASE 0xFFFFFFFF7FFFFFF0

NbCpuLaunchKernelAsm:
    //li sp, NB_KE_STACK_BASE
    mv t0, a0
    mv a0, a1
    csrw mepc, t0
    mret

.end
