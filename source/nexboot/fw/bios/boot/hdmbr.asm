; hdmbr.S - contains MBR for hard disks
; Copyright 2022, 2023 The NexNix Project
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
org 0x600

; Global constants
%define HDMBR_BASE 0x600
%define HDMBR_BIOS_BASE 0x7C00
%define HDMBR_WORDS 0x100
%define HDMBR_STACK_TOP 0x7B00
%define HDMBR_PARTTAB_START 0x7BE
%define HDMBR_ACTIVE (1 << 7)
%define HDMBR_ENTRY_SIZE 16
%define HDMBR_VBR_BASE 0x7C00
%define HDMBR_VBR_SIG 0x7DFE
%define HDMBR_BIOS_SIG 0xAA55

mbrBase:
    jmp start
    nop
; Some BIOSes expect a valid BPB, and will trash the MBR if one isn't found.
; Account for that
times 87 db 0

%include "common.inc"

; Entry point of NexNix
start:
    ; Setup basic state
    cld
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, HDMBR_STACK_TOP
    ; Relocate us
    mov si, HDMBR_BIOS_BASE
    mov di, HDMBR_BASE
    mov cx, HDMBR_WORDS
    rep movsw
    ; Jump to new position, setting CS in the process
    mov ax, HdMbrEntry
    push 0
    push ax
    retf
HdMbrEntry:
    mov bp, sp
    ; Save DL
    push dx
    sti
    ; Find active partition
    mov si, HDMBR_PARTTAB_START
    mov cx, 4
.activeLoop:
    mov al, [si]
    test al, HDMBR_ACTIVE
    jnz .activeFound
    add si, HDMBR_ENTRY_SIZE
    loop .activeLoop
    ; Print error, as there is no active partition
    mov si, msgPrefix
    call BootPrint
    mov si, bootPartErr
    call BootPrint
    jmp .reboot
.activeFound:
    ; Get partition start
    mov ax, [si+8]
    push ax             ; Save sector low
    mov bx, [si+10]
    push bx             ; Save sector high
    ; Attempt to read using LBA BIOS
    ; WARNING: We run extended read, and then if carry is set or AH contains 0x86
    ; or 0x80, we fall back to CHS
    ; If some old BIOS uses AH = 0x42 for some other functions, who knows
    ; what will happen
    ; RBIL doesn't list 42h for anything else, so I think we're OK
    mov si, dap
    mov [si+8], ax      ; Save LBA low to DAP
    mov [si+10], bx     ; Save LBA high to DAP
    mov dx, [bp-2]      ; Restore drive number
    mov ah, 0x42        ; BIOS extended read
    int 0x13            ; Read it in
    jc .chsBios         ; Carry set? Run CHS function
    cmp ah, 0x86        ; Check for error code
    je .chsBios
    cmp ah, 0x80
    je .chsBios
    jmp .launchVbr
.chsBios:
    ; Now we must convert it to CHS.
    ; This is quite complex, to start, obtain the BIOS'es geometry
    mov dx, [bp-2]      ; Restore drive number
    mov ah, 8           ; BIOS get disk geometry
    int 0x13
    jc .diskError
    and cl, 0x3F        ; Clear upper 2 bits of cylinder to get SPT
    xor ch, ch          ; Clear CH
    push cx             ; Save SPT
    add dh, 1           ; Get HPC
    mov dl, dh          ; Make HPC low byte
    xor dh, dh          ; Clear high byte
    push dx             ; Sav HPC
    ; Get LBA / SPT
    mov ax, [bp-4]        ; Place low 16 of LBA in AX
    mov dx, [bp-6]        ; Place upper 16 of LBA in DX
    div cx                ; Get LBA over SPT
    push ax               ; Save LBA / SPT
    push dx               ; Save LBA % SPT
    ; Get cylinder and head 
    xor dx, dx          ; Clear DX
    div word [bp-10]    ; Get them
    push ax             ; Save cylinder
    push dx             ; Save head
    ; Get sector
    add word [bp-14], 1 ; Add 1 to LBA % SPT
    mov ax, [bp-14]
    push ax             ; Re-push it
    ; Call BIOS to read in sector now
    ; Reset disk system
    mov dx, word [bp-16]
    mov dl, ch          ; Store cylinder low CH for BIOS
    and dh, 3           ; Clear top 6 bits. Cylinder high is in DH
    mov bx, word [bp-20]   ; Get sector
    mov cl, bl          ; Stick sector in CL for BIOS
    shl dh, 6           ; Move cylinder high to top two bits of DH
    and dh, 0xC0
    or cl, dh           ; Stick cylinder high in CL
    mov dx, [bp-18]     ; Get head
    mov dh, dl          ; Stick head in DH
    mov dl, [bp-2]      ; Get drive number in DL
    mov bx, HDMBR_VBR_BASE    ; VBR base goes in BX
    xor si, si
    mov es, si
    mov ah, 2           ; BIOS read sector
    mov al, 1           ; Sector count
    int 0x13            ; Read it in
    jc .diskError       ; Did an error occur?
.launchVbr:
    ; VBR is now read, check signature
    cmp word [HDMBR_VBR_SIG], HDMBR_BIOS_SIG
    jne .invalidSig
    ; Restore DL
    mov dx, word [bp-2]
    ; Off to VBR land we go!
    jmp HDMBR_VBR_BASE
.diskError:
    mov si, msgPrefix
    call BootPrint
    mov si, diskErrMsg
    call BootPrint
    jmp .reboot
.invalidSig:
    mov si, msgPrefix
    call BootPrint
    mov si, sigMsg
    call BootPrint
    jmp .reboot
.reboot:
    ; Wait for key strike and reboot
    mov ah, 0
    int 0x16
    int 0x18

dap:
    db 0x10
    db 0
    dw 1
    dw 0x7C00
    dw 0
    dq 0
msgPrefix: db "hdmbr: ", 0
bootPartErr: db "No boot partition", 0
diskErrMsg: db "Disk error", 0
sigMsg: db "Invalid VBR signature", 0

; Pad out to partition table start
times 440 - ($ - $$) db 0
