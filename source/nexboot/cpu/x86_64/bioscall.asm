; bioscall.asm - contains code to call BIOS interrupts
; Copyright 2023 - 2024 The NexNix Project
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

org 0x1000
section .realmode
bits 64

%define BIOS_STACK_TOP 0xDFF0

NbBiosCall:
    ; Save stack frame
    push rbp
    mov rbp, rsp
    ; Save registers
    push rbx
    ; Step 1 - store long mode state
    sidt [idtStore]
    sgdt [gdtStore]
    mov [pmodeStack], rsp
    mov rax, cr3
    mov [cr3Val], rax
    ; Step 2 - switch to real mode state
    ; Get register input
    mov rbx, rdx
    mov rcx, rdi
    mov rdx, rsi
    ; Copy to new stack
    mov rsp, BIOS_STACK_TOP
    push qword [cr3Val]
    push gdtStore
    mov edi, esp
    ; Go to compatibility mode
    push word 0x18
    push qword .32bitmode
    retfq
bits 32
.idt16:
    dw 0x3FF
    dd 0
align 16
.32bitmode:
    ; Set segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, edi        ; Restore saved ESP
    lidt [.idt16]       ; Load IVT
    ; Load input registers
    push dword [edx]
    push dword [edx+4]
    push dword [edx+8]
    push dword [edx+12]
    push dword [edx+16]
    push dword [edx+20]
    push word [edx+24]
    push word [edx+26]
    push ebx
    mov edi, esp        ; Save ESP
    ; Disable paging to deactivate long mode
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax
    ; Turn off EFER.LME
    push ecx
    mov ecx, 0xC0000080
    rdmsr
    and eax, ~(1 << 8)
    wrmsr
    pop ecx
    ; Go to 16 bit protected mode
    jmp 0x08:.16bitmode
bits 16
.16bitmode:
    ; Set segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BIOS_STACK_TOP
    ; Clear PE bit
    mov eax, cr0
    and eax, ~(1 << 0)
    mov cr0, eax
    ; And to real mode
    jmp 0:.realmode
.realmode:
    ; Set segments
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, edi
    ; Modify int
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
    mov esp, BIOS_STACK_TOP
    sti
    ; Execute interrupt
.int:
    int 0                   ; Modified to right interrupt
    cli
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
    mov ax, 0
    mov ds, ax      ; Ensure DS is right
    lgdt [gdtStore]
    lidt [idtStore]
    ; Enable Pmode
    mov eax, cr0
    or eax, (1 << 0)
    mov cr0, eax
    jmp 0x18:.toPmode   ; Off we go!
bits 32
.toPmode:
    ; Set segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
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
    ; Set CR3
    mov eax, dword [cr3Val]
    mov cr3, eax
    ; Set EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr
    ; Set CR0.PG
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax
    ; Back to long mode!
    jmp 0x28:.toLmode
bits 64
.toLmode:
    ; Restore stack
    mov rsp, [pmodeStack]
    ; Restore registers
    pop rbx
    ; Return to caller
    pop rbp
    ret

; Stored spots of IDT and GDT
idtStore: times 6 db 0
gdtStore: times 6 db 0
cr3Val: dq 0
pmodeStack: dq 0

; IDT
idtr64:
    dw 0
    dq 0

times 0x1000 - ($$-$) db 0

NbBiosCallMbr:
    mov rsp, BIOS_STACK_TOP
    ; Go to compatibility mode
    push word 0x18
    push qword .32bitmode
    retfq
bits 32
.idt16:
    dw 0x3FF
    dd 0
align 16
.32bitmode:
    ; Set segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, BIOS_STACK_TOP        ; Restore saved ESP
    lidt [.idt16]       ; Load IVT
; Disable paging to deactivate long mode
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax
    ; Turn off EFER.LME
    push ecx
    mov ecx, 0xC0000080
    rdmsr
    and eax, ~(1 << 8)
    wrmsr
    pop ecx
    ; Go to 16 bit protected mode
    jmp 0x08:.16bitmode
bits 16
.16bitmode:
    ; Set segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BIOS_STACK_TOP
    ; Clear PE bit
    mov eax, cr0
    and eax, ~(1 << 0)
    mov cr0, eax
    ; And to real mode
    jmp 0:.realmode
.realmode:
    ; Set segments
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, BIOS_STACK_TOP
    mov dx, di
    jmp 0:0x7C00
