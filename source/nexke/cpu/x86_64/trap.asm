; trap.asm - contains trap handlers
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

; Trap handler macros
%macro TRAP_NOERR 1
CpuTrap%1:
    push qword strict 0            ; Dummy error code
    push qword strict %1           ; Interrupt number
    jmp CpuTrapCommon       ; Go to common handler
align 16
%endmacro

%macro TRAP_ERR 1
CpuTrap%1:
    push qword strict %1           ; Interrupt number
    jmp CpuTrapCommon       ; Go to common handler
align 16
%endmacro

; ISR table
; The idea here is that all trap stubs are padded to 16 bytes,
; so we can easily reference them as a table from C code
global CpuTrapTable
CpuTrapTable:
%assign i 0
%rep 256
; Handle error code interrupts
; Exceptions 8 - 14 all push an error code, with the exception (no pun intended) of exception 9
%if (i >= 8 && i <= 14) && (i != 9) 
    TRAP_ERR i
; 17 and 21 also push error codes
%elif i == 17 || i == 21
    TRAP_ERR i
%else
    TRAP_NOERR i        ; Default is to not push error code
%endif
%assign i i+1
%endrep

extern PltTrapDispatch
; Common trap handler
CpuTrapCommon:
    push rax                ; Save GPRs
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    xor rbp, rbp            ; Set up base frame
    mov rdi, rsp            ; Pass context structure
    call PltTrapDispatch    ; Call trap dispatcher
    pop r15                 ; Restore GPRs
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16         ; Cleanup error code and interrupt number
    iretq               ; Return to whatever was happening
