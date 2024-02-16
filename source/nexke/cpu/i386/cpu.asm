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
    push ebp
    mov ebp, esp
    ; Get GDTR
    mov eax, [ebp+8]
    ; Load it
    lgdt [eax]
    ; Flush segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flushCs
.flushCs:
    pop ebp
    ret

; Flushes the IDT
global CpuInstallIdt
CpuInstallIdt:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    lidt [eax]      ; Load it
    pop ebp
    ret