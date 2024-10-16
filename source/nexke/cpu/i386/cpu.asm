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
    mov ss, ax
    ; Set GS
    mov ax, 0x28
    mov gs, ax
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

; Flushes TLB entry
global MmMulFlush
MmMulFlush:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    invlpg [eax]
    pop ebp
    ret

; Detects CPUID
global CpuCheckCpuid
CpuCheckCpuid:
    push ebp
    mov ebp, esp
    ; Detect wheter we are on a CPU with CPUID or not
    ; Intel says that we must check if EFLAGS.ID is modifiable to determine
    pushfd                          ; Get EFLAGS
    pop eax
    xor eax, 1 << 21                ; Change EFLAGS.ID
    mov ecx, eax                    ; Store for reference
    push eax                        ; Store new EFLAGS
    popfd
    pushfd                          ; Get new EFLAGS
    pop eax
    cmp eax, ecx                    ; Check if they're different
    je .cpuid
    mov eax, 0                      ; Return false
    jmp .done
.cpuid:
    mov eax, 1                      ; Return true
.done:
    pop ebp
    ret

; Detects a 486
global CpuCheck486
CpuCheck486:
    push ebp
    mov ebp, esp
    ; How do we decide if this is a 486? Based on wheter EFLAGS.AC is modifiable
    pushfd                          ; Get EFLAGS
    pop eax
    xor eax, 1 << 18                ; Change EFLAGS.AC
    mov ecx, eax                    ; Store for reference
    push eax                        ; Store new EFLAGS
    popfd
    pushfd                          ; Get new EFLAGS
    pop eax
    cmp eax, ecx                    ; Check if they're different
    je .is486
    mov eax, 0                      ; Return false
    jmp .done
.is486:
    mov eax, 1                      ; Return true
.done:
    pop ebp
    ret

; Detects an FPU
global CpuCheckFpu
CpuCheckFpu:
    push ebp
    mov ebp, esp
    mov ax, 0xFFFF
    fninit                          ; Initialize it
    fstsw ax                        ; Get status word
    cmp al, 0                       ; Check if its 0
    je .fpuExists                   ; If equal, FPU exists
    mov eax, 0                      ; Return false
    jmp .done
.fpuExists:
    mov eax, 1                      ; Return true
.done:
    pop ebp
    ret

; Performs a context switch
; Very critical code path
global CpuSwitchContext
CpuSwitchContext:
    ; Get new and old context pointers
    mov eax, [esp+8]
    mov edx, [esp+4]
    ; Save registers
    push ebp
    push ebx
    push esi
    push edi
    ; Save old stack
    mov [eax], esp
    ; Set new stack
    mov esp, edx
    ; Restore old context
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
