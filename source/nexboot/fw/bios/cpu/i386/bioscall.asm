; bioscall.asm - contains code to call BIOS interrupts
; Copyright 2023 The NexNix Project
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

org 0x2000
section .realmode
bits 32

%define BIOS_STACK_TOP 0x8000

NbBiosCall:
    ; Save stack frame
    push ebp
    mov ebp, esp
    pusha
    ; Step 1 - store protected mode state
    sidt [idtStore]
    sgdt [gdtStore]
    mov [pmodeStack], esp
    mov eax, cr3
    mov [cr3Val], eax
    ; Step 2 - switch to real mode state
    lidt [ivt]                  ; Load IVT
    ; Turn off paging
    mov eax, cr0
    and eax, ~(0x80000000)
    mov cr0, eax
    ; Get register input
    mov eax, [ebp+12]
    mov ebx, [ebp+16]
    mov ecx, [ebp+8]
    ; Load up 16 bit stack
    mov esp, BIOS_STACK_TOP
    mov ebp, esp
    ; Copy to new stack
    push dword [cr3Val]
    push gdtStore
    push dword [eax]
    push dword [eax+4]
    push dword [eax+8]
    push dword [eax+12]
    push dword [eax+16]
    push dword [eax+20]
    push word [eax+24]
    push word [eax+26]
    push ebx
    push ecx
    ; Jump to 16 bit mode protected mode
    jmp 0x08:.16bitpmode
bits 16
.16bitpmode:
    ; Clear PE bit
    mov eax, cr0
    and eax, ~(1 << 0)
    mov cr0, eax
    ; And to real mode
    jmp 0:.realmode
.realmode:
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; Adjust interrupt number
    pop ecx
    mov [.int+1], cl
    ; Store output
    pop ebp
    ; Store registers
    pop es
    pop ds
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ; Execute interrupt
.int:
    int 0                   ; Modified to right interrupt
    ; Push output
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ds
    push es
    pushf
    ; Back to protected mode
    lgdt [gdtStore]         ; Restore GDT
    lidt [idtStore]         ; Restore IDT
    mov eax, [cr3Val]
    mov cr3, eax            ; Restore CR3
    mov eax, cr0
    or eax, 1
    mov cr0, eax            ; Enable PMode
    jmp 0x18:.toPmode       ; ... and to protected mode we go
bits 32
.toPmode:
    ; Set segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; Enable paging again
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax
    ; Restore output to right data structure
    pop word [ebp+28]
    pop word [ebp+26]
    pop word [ebp+24]
    pop dword [ebp+20]
    pop dword [ebp+16]
    pop dword [ebp+12]
    pop dword [ebp+8]
    pop dword [ebp+4]
    pop dword [ebp]
    ; Restore stack
    mov esp, [pmodeStack]
    ; And exit
    popa
    pop ebp
    ret

; Stored spots of IDT and GDT
idtStore: times 6 db 0
gdtStore: times 6 db 0
cr3Val: dd 0
pmodeStack: dd 0

; Real mode IVT
ivt:
    dw 0x3FF
    dd 0
