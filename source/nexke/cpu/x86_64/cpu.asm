; cpu.asm - contains CPU low-level ASM functions
; Copyright 2024 The NexNix Project
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

section .text

; Flushes the GDT
global CpuFlushGdt
CpuFlushGdt:
    ; Load it
    lgdt [rdi]
    ; Flush segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    push 0x08
    push .flushCs
    retfq
.flushCs:
    ret

; Flushes the IDT
global CpuInstallIdt
CpuInstallIdt:
    lidt [rdi]      ; Load it
    ret

; Flushes TLB entry
global MmMulFlush
MmMulFlush:
    invlpg [rdi]
    ret

; Performs a context switch
; Very critical code path
global CpuSwitchContext
CpuSwitchContext:
    ; New context is in RDI, old in RSI
    ; Save callee saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    ; Save old stack
    mov [rsi], rsp
    ; Load new stack
    mov rsp, rdi
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret         ; Start at RIP of new context
