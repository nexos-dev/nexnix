; nbload.S - contains nbload logic
; Copyright 2022 - 2024 The NexNix Project
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

bits 16
section .text

; Start of bootloader detection data
%define NBLOAD_DETECT_RESULT 0x700
; Top of stack
%define NBLOAD_STACK_TOP 0x60000
; Log-related bits
%define NBLOAD_LOG_START 0x612
%define NBLOAD_LOG_SIZE 0x100

%define NBLOAD_PMODE_ENTRY 0x11000
%define NBLOAD_LMODE_ENTRY NBLOAD_PMODE_ENTRY
%define NBLOAD_BASE 0x10000
%define NBLOAD_BASE_SEG 0x1000

; Paging structure locations
%ifdef NEXNIX_ARCH_X86_64
%ifdef NEXNIX_X86_64_LA57
%define NBLOAD_PML5_BASE 0x3000
%define NBLOAD_PML4_BASE 0x4000
%define NBLOAD_PDPT_BASE 0x5000
%define NBLOAD_PDIR_BASE 0x6000
%else
%define NBLOAD_PML4_BASE 0x3000
%define NBLOAD_PDPT_BASE 0x4000
%define NBLOAD_PDIR_BASE 0x5000
%endif
%else
%ifndef NEXNIX_I386_PAE
%define NBLOAD_PDIR_BASE 0x3000
%define NBLOAD_PTAB_BASE 0x4000
%define NBLOAD_PTAB2_BASE 0x5000
%else
%define NBLOAD_PDIR_BASE 0x3000
%define NBLOAD_PDPT_BASE 0x4000
%endif
%endif

%define NBLOAD_TABLE_ACPI    0
%define NBLOAD_TABLE_PNP     1
%define NBLOAD_TABLE_APM     2
%define NBLOAD_TABLE_MPS     3
%define NBLOAD_TABLE_SMBIOS  4
%define NBLOAD_TABLE_SMBIOS3 5
%define NBLOAD_TABLE_BIOS32  6

; CPU families
%define NBLOAD_CPU_FAMILY_X86 1

; CPU archutectures
%define NBLOAD_CPU_ARCH_I386   1
%define NBLOAD_CPU_ARCH_X86_64 2

; CPU versions
%define NBLOAD_CPU_VERSION_386   1
%define NBLOAD_CPU_VERSION_486   2
%define NBLOAD_CPU_VERSION_CPUID 3

; CPU flags
%define NBLOAD_CPU_FLAG_FPU_EXISTS (1 << 0)

%define NBLOAD_SIGNATURE 0xDEADBEEF

NbStartDetect:
    ; Set state
    mov ax, NBLOAD_BASE_SEG
    mov ds, ax
    mov ax, 0
    mov ss, ax
    mov es, ax
    mov sp, 0xFFF0
    ; Save size
    push es
    mov ax, NBLOAD_BASE_SEG
    mov es, ax
    mov di, nexbootSz
    mov [es:di], ecx
    pop es
    ; Save drive number
    mov bp, sp
    push dx
    ; Zero out log
    mov di, NBLOAD_LOG_START
    mov cx, NBLOAD_LOG_SIZE
    sub cx, 18
    mov al, 0
    rep stosb
    ; Initialize detection structure
    mov di, NBLOAD_DETECT_RESULT
    mov dword [es:di], NBLOAD_SIGNATURE            ; Set signature
    mov word [es:di+4], 0x600                      ; Set log start offset
    mov word [es:di+6], 0                          ; Set log start segment
    mov word [es:di+8], NBLOAD_LOG_SIZE            ; Set log size
    mov ax, word [bp-2]
    mov byte [es:di+18], al                        ; Set drive number
    ; CPU detection
    mov byte [es:di+12], NBLOAD_CPU_FAMILY_X86        ; Set CPU family
%ifdef NEXNIX_ARCH_I386
    mov byte [es:di+13], NBLOAD_CPU_ARCH_I386      ; Set CPU architecture
%elifdef NEXNIX_ARCH_X86_64
    mov byte [es:di+13], NBLOAD_CPU_ARCH_X86_64    ; Set CPU architecture
%else
%error Unsupported BIOS architecture
%endif
    ; Detect wheter we are on a CPU with CPUID, or if we are on a 486, or a 386
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
    je .isCpuid                     ; Nope, store that
    ; Print out error, CPUID required
    mov si, cpuOldMessage
    mov cx, 1
    call NbloadLogMsg
    call NbloadPanic
.isCpuid:
    mov word [es:di+14], NBLOAD_CPU_VERSION_CPUID ; Let other layers know to use CPUID
%ifdef NEXNIX_I386_PAE
    ; Check for PAE now since we know we have CPUID
    mov eax, 1
    cpuid
    ; Check for PAE flag in EDX
    test edx, 1 << 6                ; Test PAE flag
    jz .noPae                       ; If no PAE, panic
%endif
    ; Print message
    mov si, cpuCpuidMessage
    mov cx, 4
    call NbloadLogMsg
    ; Set CR0.WP
    mov eax, cr0
    or eax, 1 << 16
    mov cr0, eax
    jmp .cpuCheckDone
.noPae:
    ; Print message and panic
    mov si, noPaeMessage
    mov cx, 1
    call NbloadLogMsg
    call NbloadPanic
.cpuCheckDone:
    ; FPU check
    ; Check if an x87 coprocessor exists, and set CR0 accordingly
    fninit                          ; Initialize it
    fstsw ax                        ; Get status word
    cmp al, 0                       ; Check if its 0
    je .fpuExists                   ; If equal, FPU exists
    mov eax, cr0
    or eax, 1 << 2                  ; Set CR0.EM
    mov cr0, eax
    mov byte [es:di+16], $0            ; Inform nexboot of this
    ; As of now, software-emulated FPU isn't supported
    ; Error out if an FPU doesn't exist
    mov si, noFpuMessage
    mov cx, 1
    call NbloadLogMsg
    call NbloadPanic
    jmp .fpuCheckDone
.fpuExists:
    mov byte [es:di+16], 1 << 0         ; Inform nexboot of this
    ; Print message
    mov si, fpuMessage
    mov cx, 4
    call NbloadLogMsg
.fpuCheckDone:
%ifdef NEXNIX_ARCH_X86_64
    ; Check for long mode support
    mov eax, 0x80000000                 ; Check to ensure CPUID extended features
                                        ; are supported
    cpuid
    cmp eax, 0x80000001
    jb .noLmode
    mov eax, 0x80000001                 ; Get extended flags
    cpuid
    test edx, 1 << 29               ; Test long-mode support bit
    jz .noLmode                         ; If not supported, panic
    jmp .a20Enable
.noLmode:
    mov si, noLmodeMessage
    mov cx, 0
    call NbloadLogMsg
    call NbloadPanic
.a20Enable:
%endif
    cli
    ; Now we must enable the A20 gate
    call NbloadWaitInputBuf     ; Wait for input buffer to be empty
    mov al, 0xAD                ; Command 0xAD disables keyboard
    out 0x64, al                ; Disable it

    call NbloadWaitInputBuf     ; Wait for input buffer to be empty
    mov al, 0xD0                ; Command 0xD0 reads configuration byte
    out 0x64, al                ; Grab configuration byte

    call NbloadWaitOutputBuf    ; Wait for output buffer to be full
    in al, 0x60                 ; Read in configuration byte
    mov dl, al
    or dl, 1 << 1               ; Set A20 bit

    call NbloadWaitInputBuf     ; Wait for input buffer
    mov al, 0xD1                ; Command 0xD1 writes configuration byte
    out 0x64, al                ; Set command to write conf byte

    call NbloadWaitInputBuf     ; Wait for input buffer
    mov al, dl
    out 0x60, al                ; Write new byte
    
    call NbloadWaitInputBuf
    mov al, 0xAE                ; Command 0xAE enables keyboard
    out 0x64, al                ; Enable it
    call NbloadWaitInputBuf
    push es
    mov ax, 0xFFFF              ; Load 0xFFFF as segment
    mov es, ax
    mov word [es:0x7E0E], 0xAA88     ; Load value to high memory
    pop es
    cmp word [0x7DFE], 0xAA88
    je .a20failed               ; If equal, A20 check failed
%ifdef NEXNIX_ARCH_I386
    ; Now we must enter protected mode...
    cli                             ; Disable interrupts
    lgdt [gdtPtr]                   ; Load the GDT
    mov eax, cr0                    ; Grab CR0
    or eax, 1 << 0                  ; Set the PE bit
    mov cr0, eax                    ; Update CR0 with PE bit set
    jmp dword 0x18:NBLOAD_PMODE_ENTRY     ; Far return to protected mode
%elifdef NEXNIX_ARCH_X86_64
    cli
    ; Now we need to enter long mode
    ; First, load the GDT
    lgdt [gdtPtr]
    ; Initialize paging structures. This changes based on wheter we are using LA57 or not
    push ds                             ; Zero out DS
    mov ax, 0
    mov ds, ax
    ; Zero out everything
%ifdef NEXNIX_X86_64_LA57
    mov di, NBLOAD_PML5_BASE          ; Get PML5
    mov cx, 0x1000                    ; Zero out page
    mov al, 0
    rep stosb
%endif
    mov di, NBLOAD_PML4_BASE           ; Get PML4
    mov cx, 0x1000                     ; Zero out the whole page
    mov al, 0
    rep stosb
    mov di, NBLOAD_PDPT_BASE           ; Get PDPT
    mov cx, 0x1000                     ; Zero out a page
    mov al, 0
    rep stosb                           ; Zero it
    mov di, NBLOAD_PDIR_BASE            ; Get page directory
    mov cx, 0x1000
    mov al, 0
    rep stosb
    ; If LA57 is used, we must check for LA57, enable it if supported, and map the PML4 to the PML5
    ; If not supported, just jump to mapping
%ifdef NEXNIX_X86_64_LA57
    ; Check for CPUID function 0x7
    mov eax, 0
    cpuid
    cmp eax, 7
    jb .nola57
    ; CPUID supports function 0x7, now check for LA57
    mov eax, 7
    mov ecx, 0
    cpuid
    test ecx, 1 << 16                   ; Test LA57 bit
    jz .nola57
    ; CPU supports LA57, map PML4 into PML5
    mov ebx, NBLOAD_PML4_BASE
    mov edi, NBLOAD_PML5_BASE
    or ebx, 3                           ; Set present and writable bits
    mov [edi], ebx                      ; Move PML5E to PML5
    mov ebp, NBLOAD_PML5_BASE           ; Save PML5
    ; Enable LA57
    mov eax, cr4
    or eax, 1 << 12                    ; Set LA57 bit in CR4
    mov cr4, eax
    jmp .la57continue
.nola57:
%endif
    mov ebp, NBLOAD_PML4_BASE          ; Save PML4 address
.la57continue:
    ; Map PDPT into PML4
    mov ebx, NBLOAD_PDPT_BASE
    mov edi, NBLOAD_PML4_BASE
    or ebx, 3                           ; Set present and writable bits on entry
    mov [edi], ebx                      ; Move PML4E into PML4
    ; Map page directory into PDPT
    mov edi, NBLOAD_PDPT_BASE           ; Get PDPT address
    mov ebx, NBLOAD_PDIR_BASE           ; Get page directory address
    or ebx, 3                           ; Mark as present and writable
    mov [edi], ebx                      ; Map it
    ; Map page into directory
    mov edi, NBLOAD_PDIR_BASE           ; Get page directory address
    mov ebx, 0x83                       ; Set physical address to 0, present and
                                        ; writable bits to 0, and size bit to 1
    mov [edi], ebx                      ; Put in directory
    mov ebx, 0x200083                   ; Map another 2M
    mov [edi+8], ebx
    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5                      ; Set PAE bit
    mov cr4, eax
    ; Load saved PML4 or PML5 from EBP
    mov cr3, ebp                        ; Load top-level paging structure
    ; Store nexbootSz
    mov ax, NBLOAD_BASE_SEG
    mov es, ax
    mov di, nexbootSz
    mov esi, [es:di]
    ; Set the LME bit in EFER
    mov ecx, 0xC0000080                 ; Load MSR to read
    rdmsr                               ; Read MSR
    or eax, 1 << 8                      ; Set LME bit
    wrmsr                               ; Write it out
    ; Enable PE and PG bits at the same time to activate long mode
    mov eax, cr0
    or eax, 0x80000001                  ; Set both bits
    mov cr0, eax                        ; Activate long mode
    jmp dword 0x28:NBLOAD_LMODE_ENTRY   ; Far jump to flush CS
%endif
.a20failed:
    mov si, a20failMessage
    mov cx, 1
    call NbloadLogMsg           ; Print out error
    call NbloadPanic

%include "common.inc"

; Waits for keyboard input buffer
NbloadWaitInputBuf:
.wait:
    in al, 0x64                 ; Read flags
    test al, 1 << 1             ; Check input buffer empty
    jnz .wait
    ret

; Waits for keyboard output buffer
NbloadWaitOutputBuf:
.wait2:
    in al, 0x64                 ; Read flags
    test al, 1 << 0             ; Check output buffer full
    jz .wait2
    ret

; Waits for key strike and goes to next boot device
NbloadPanic:
    mov ah, 0
    int 0x16
    int 0x18

; Logs a message to boot log
; Input: DS:SI = string to write, CX = log level of message
NbloadLogMsg:
    pusha
    ; Write message out to log
    mov bx, si                  ; Save SI
    mov si, word [logLocation]
    push es
    mov dx, 0                   ; So we can access log
    mov es, dx
    ; Format of log entry:
    ; Offset 0 - 1: Message offset
    ; Offset 2 - 3: Message segment
    ; Offset 4 - 5: Log level
    mov [es:si], bx                ; Write message offset
    mov [es:si+2], ds              ; Write segment
    mov [es:si+4], cx              ; Write log level
    pop es
    ; Write out new log location
    add si, 6                   ; Move to next log entry
    mov word [logLocation], si
    mov si, bx                  ; Get message back in SI
    ; Decide where to print
    cmp cx, NEXNIX_LOGLEVEL
    jg .logDone                    ; Print to serial port if message level
                                ; is greater than system level
    call BootPrint              ; Print to screen if allowed, then to serial port
.logDone:
    popa
    ret

biosDap:
    db 16
    db 0
    db 1
    db 0
    dw 0x800          ; Offset to load into
    dw 0          ; Segment to load into
    dq 0         ; Sector to read

; Size of nexboot file
nexbootSz: dd 0

; The current log pointer
logLocation: dw NBLOAD_LOG_START

cpuCpuidMessage: db 0x0A, 0x0D, "nbload: detected CPU 486+", 0
cpuOldMessage: db 0x0A, 0x0D, "nbload: CPU with CPUID required", 0
fpuMessage: db 0x0A, 0x0D, "nbload: x87 FPU found", 0x0A, 0x0D, 0
noFpuMessage: db 0x0A, 0x0D, "nbload: no x87 FPU found", 0
a20failMessage: db 0x0A, 0x0D, "nbload: unable to enable A20 gate", 0
noPaeMessage: db 0x0A, 0x0D, "nbload: PAE not supported on this CPU. Please use non-PAE images", 0
%ifdef NEXNIX_ARCH_X86_64
noLmodeMessage: db 0x0A, 0x0D, "nbload: long mode not supported on this CPU. Please use 32-bit images", 0
%endif

%ifdef NEXNIX_ARCH_I386
; Protected mode data structures

; The global descriptor table
gdtBase:
    ; Null descriptor
    dw 0
    dw 0
    db 0
    db 0
    db 0
    db 0
    ; 16 bit code segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0x0                   ; High 8 bits of base
    db 0x98                  ; Access byte. Execute-only code segment
                             ; that is non-conforming and present
    db 0                     ; Reserved
    db 0                     ; Reserved
    ; 16 bit data segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0x0                   ; High 8 bits of base
    db 0x92                  ; Read-write data segment
    db 0                     ; Reserved
    db 0                     ; Reserved
    ; 32 bit code segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x98                  ; Access byte. Execute-only code segment
                             ; that is non-conforming and present
    db 0xCF                  ; Top 4 bits of limit = 0xF, granularity = 4K,
                             ; D bit = 1, indicating a 32 bit code segment
    db 0                     ; Top 8 bits of base
    ; 32 bit data segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x92                  ; Read write data segment
    db 0xCF                  ; Top 4 bits of limit = 0xF, granularity = 4K,
                             ; B bit = 1, indicating 32 bit stack when in SS
    db 0                     ; Top 8 bits of base
gdtEnd:

; GDTR structure
gdtPtr:
    dw gdtEnd - gdtBase - 1          ; Limit = last addressable byte in table
    dd gdtBase + 0x10000             ; Start of GDT

align 4096
bits 32

; Memory locations
%define NBLOAD_PMODE_STACK 0x7FFFC
%define NBLOAD_NEXBOOT_BASE 0x50000

; Protected mode entry point
NbloadStartPmode:
    ; Set data segments
    mov ax, 0x20                    ; 0x20 = data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Set up a stack
    mov esp, NBLOAD_STACK_TOP
    ; Set up paging structures
%ifdef NEXNIX_I386_PAE
    mov esi, NBLOAD_PDPT_BASE       ; Get PDPT address
    mov edi, esi                    ; Prepare to zero it
    mov al, 0
    mov ecx, 32                     ; 32 bytes to 0
    rep stosb
    ; Map directory in PDPT
    mov ebx, NBLOAD_PDIR_BASE       ; Get address of page directory
    mov edi, ebx                    ; Prepare to zero it
    mov al, 0
    mov ecx, 0x1000                 ; Zero out a page
    rep stosb
    or ebx, 1                       ; Mark as present
    mov [esi], ebx                  ; Map page directory. Note that we only work with
                                    ; the low 32 bits and leave the top bits zeroed
    ; Create PDE
    mov esi, NBLOAD_PDIR_BASE       ; Get page directory address
    mov eax, 0x83                   ; Prepare entry. NOTE: we only use low 4 bytes
                                    ; of entry, leaving upper 4 zeroed
                                    ; Entry is present, writable, and large
                                    ; Address mapped is address 0
    mov [esi], eax                  ; Map it
    or eax, 0x200000                ; Set physical address of entry to 2 MiB
    mov [esi+8], eax                ; Map that
    ; Enable paging now
    mov eax, cr4                    ; Get CR4 to enable PAE
    or eax, 1 << 5                  ; Set PAE bit
    mov cr4, eax                    ; Put in CR4
    mov edx, NBLOAD_PDPT_BASE
    mov cr3, edx                    ; Load CR3
    mov eax, cr0                    ; Get CR0
    or eax, 1 << 31                 ; Set PG bit
    mov cr0, eax                    ; Enable paging
%else
    mov esi, NBLOAD_PDIR_BASE       ; Get page directory address
    mov edi, esi                    ; Prepare to zero it
    mov al, 0
    mov ecx, 0x1000                 ; 4096 bytes to 0
    rep stosb
    ; Create PDE for mapping
    mov ebx, NBLOAD_PTAB_BASE       ; Get address of page table
    mov edi, ebx                    ; Prepare to zero it
    mov al, 0
    mov ecx, 0x1000
    rep stosb
    or ebx, 3                       ; Set present and writable bits
    mov [esi], ebx                  ; Put in page directory
    ; Fill page table
    mov edi, NBLOAD_PTAB_BASE       ; Get page table address
    mov ecx, 1024                   ; 1024 PTEs in a page table
    mov edx, 0                      ; Start at address 0
.ptLoop:
    mov ebx, edx                    ; Get address in EBX
    or ebx, 3                       ; Set present and writable bits
    mov [edi], ebx                  ; Store in page table
    add edx, 0x1000                 ; Move to next page
    add edi, 4                      ; Move to next PTE
    loop .ptLoop
    mov cr3, esi                    ; Load page directory
    mov eax, cr0                    ; Get CR0
    or eax, 1 << 31                 ; Set PG bit
    mov cr0, eax                    ; Enable paging
%endif
    ; Now we must load up the ELF decompressor
    ; Get base of it
    mov esi, end                    ; Get end of this part
    add esi, NBLOAD_BASE
    mov ebp, esi
    mov ebx, [esi+28]               ; Get program header offset
    add ebx, esi                    ; Add base of ELF
    push esi
    mov edx, [ebx+4]                ; Get program header data offset from phdr
    add edx, esi
    mov esi, edx                    ; So we can use it rep movsb
    mov ecx, [ebx+16]               ; Get section file size
    mov edi, [ebx+8]                ; Get nexboot base address
    rep movsb                       ; Copy out
    ; Get difference between memory and file size, and zero that out
    mov edx, [ebx+20]               ; Get memory size
    mov ecx, [ebx+16]               ; Get file size
    sub edx, ecx                    ; Subtract
    mov edi, NBLOAD_NEXBOOT_BASE    ; Get nexboot base in EDI
    add edi, ecx                    ; Move to area to be zeroed
    mov ecx, edx                    ; Set loop counter to memory-file difference
    mov al, 0                       ; Store a zero
    rep stosb                       ; Store it
    ; Get entry point
    pop esi
    mov ebx, [esi+24]
    ; Find end of ndecomp
    mov edx, 0
    mov edi, [esi+0x20]
    add edi, esi
    ; Get size of table
    movzx eax, word [esi+0x2E]
    movzx ecx, word [esi+0x30]
    mul ecx
    add edi, eax
    ; Get size of nexboot
    mov ecx, [nexbootSz+NBLOAD_BASE]
    mov eax, edi            ; Get base of nexboot
    sub eax, NBLOAD_BASE    ; Subtract base of base
    sub ecx, eax            ; Subtract ndecomp and and nbload size
    mov ebp, 0                      ; Set up zero frame
    mov esp, NBLOAD_STACK_TOP
    push ecx
    push edi
    push dword NBLOAD_DETECT_RESULT
    call ebx                         ; Jump to it
end:
%elifdef NEXNIX_ARCH_X86_64
; Long mode data structures

; Global descriptor table
gdtBase:
    ; Null descriptor
    dw 0
    dw 0
    db 0
    db 0
    db 0
    db 0
    ; 16 bit code segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0x0                   ; High 8 bits of base
    db 0x98                  ; Access byte. Execute-only code segment
                                ; that is non-conforming and present
    db 0                     ; Reserved
    db 0                     ; Reserved
    ; 16 bit data segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0x0                   ; High 8 bits of base
    db 0x92                  ; Read-write data segment
    db 0                     ; Reserved
    db 0                     ; Reserved
    ; 32 bit code segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x98                  ; Access byte. Execute-only code segment
                             ; that is non-conforming and present
    db 0xCF                  ; Top 4 bits of limit = 0xF, granularity = 4K,
                             ; D bit = 1, indicating a 32 bit code segment
    db 0                     ; Top 8 bits of base
    ; 32 bit data segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x92                  ; Read write data segment
    db 0xCF                  ; Top 4 bits of limit = 0xF, granularity = 4K,
                             ; B bit = 1, indicating 32 bit stack when in SS
    db 0                     ; Top 8 bits of base
    ; 64 bit code segment descriptor
    dw 0                     ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x98                  ; Access byte. Execute-only code segment
                                ; that is non-conforming and present
    db 0xA0                  ; Top 4 bits of limit = 0xF, granularity = 4K,
                                ; L bit = 1, indicating long mode segment
    db 0                     ; Top 8 bits of base
    ; 64 bit data segment descriptor
    dw 0xFFFF                ; Low 16 bits of limit
    dw 0                     ; Low 16 bits of base
    db 0                     ; Middle 8 bits of base
    db 0x92                  ; Read write data segment
    db 0                     ; Top 4 bits of limit = 0xF, granularity = 4K,
                                ; B bit = 1
    db 0                     ; Top 8 bits of base
gdtEnd:

gdtPtr:
    dw gdtEnd - gdtBase - 1
    dq gdtBase + 0x10000

align 4096
bits 64

; Memory locations
%define NBLOAD_LMODE_STACK 0x7FFFC
%define NBLOAD_NEXBOOT_BASE 0x50000

NbloadStartLmode:
    ; Store nexbootSz
    mov r8, rsi
    ; Load data segments
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, NBLOAD_LMODE_STACK     ; Load stack pointer
    ; Now we must load up the ELF bootloader
    mov rsi, end                    ; Get end of this part
    add rsi, NBLOAD_BASE
    mov rbp, rsi
    mov rbx, [rsi+32]               ; Get program header offset
    add rbx, rsi                    ; Add base of ELF
    push rsi
    mov rdx, [rbx+8]                ; Get program header data offset from phdr
    add rdx, rsi
    mov rsi, rdx                    ; So we can use it rep movsb
    mov rcx, [rbx+32]               ; Get section file size
    mov rdi, [rbx+16]                ; Get nexboot base address
    rep movsb                       ; Copy out
    ; Get difference between memory and file size, and zero that out
    mov rdx, [rbx+40]               ; Get memory size
    mov rcx, [rbx+32]               ; Get file size
    sub rdx, rcx                    ; Subtract
    mov rdi, NBLOAD_NEXBOOT_BASE    ; Get nexboot base in EDI
    add rdi, rcx                    ; Move to area to be zeroed
    mov rcx, rdx                    ; Set loop counter to memory-file difference
    mov al, 0                       ; Store a zero
    rep stosb                       ; Store it
    ; Get entry point
    pop rsi
    mov rbx, [rsi+24]
    ; Find end of ndecomp
    mov rdx, 0
    mov rdi, [rsi+40]
    add rdi, rsi
    ; Get size of table
    movzx rax, word [rsi+58]
    movzx rcx, word [rsi+60]
    mul rcx
    add rdi, rax
    ; Get size of nexboot
    mov rcx, r8
    mov rax, rdi            ; Get base of nexboot
    sub rax, NBLOAD_BASE    ; Subtract base of base
    sub rcx, rax            ; Subtract ndecomp and and nbload size
    mov rbp, 0                      ; Set up zero frame
    mov rsp, NBLOAD_STACK_TOP
    mov rdx, rcx
    mov rsi, rdi
    mov rdi, NBLOAD_DETECT_RESULT
    call rbx                         ; Jump to it
end:
%endif
