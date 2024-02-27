; nbload.inc - contains common code for stage 1 / 1.5
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

%include "common.inc"
%define NEXNIX_VERSION "0.0.1"

; Memory layout
; Address 0x0 - 0x5FF is reserved. Note that 0x500 - 0x5FF _could_ be used, but some
; old BIOSes use it for PrtScr status
; Address 0x600 - 0x7C00 are used for our purposes. We reserve a 4 KiB stack from 0x7C00
; down to 0x6C00. 
; Everything beneath there is used for whatever we need
; Note that we don't have an official memory allocation system; we hard code what we need
; to specific addresses
%define NBLOAD_STACK_TOP 0x7C00
%define NBLOAD_FREEMEM_BASE 0x600
%define NBLOAD_FREEMEM_TOP 0x6BFF
%define NBLOAD_PART2_BASE 0x7E00
%define NBLOAD_FAT_ROOTDIR_BASE 0x8000
%define NBLOAD_FAT_FAT_BASE 0x8000
%define NBLOAD_NEXBOOT_SEG 0x1000

; Waits for key strike and goes to next boot device
NbloadPanic:
    mov ah, 0
    int 0x16
    int 0x18

%define NBLOAD_LOG_START 0x600
%define NBLOAD_LOG_SIZE 0x100

; Logs a message to boot log
; Input: DS:SI = string to write, CX = log level of message
NbloadLogMsg:
    pusha
    push es
    mov ax, 0
    mov es, ax
    ; Write message out to log
    mov bx, si                  ; Save SI
    mov si, word [logLocation]
    ; Format of log entry:
    ; Offset 0 - 1: Message offset
    ; Offset 2 - 3: Message segment
    ; Offset 4 - 5: Log level
    mov [es:si], bx              ; Write message offset
    mov [es:si+2], ds            ; Write segment
    mov [es:si+4], cx            ; Write log level
    ; Write out new log location
    add si, 6                 ; Move to next log entry
    mov word [logLocation], si     ; Store it
    mov si, bx                ; Get message back in SI
    ; Decide where to print
    cmp cx, NEXNIX_LOGLEVEL
    jg .logDone                    ; Print to serial port if message level
                                ; is greater than system level
    call BootPrint              ; Print to screen if allowed, then to serial port
.logDone:
    pop es
    popa
    ret
; The current log pointer
logLocation: dw NBLOAD_LOG_START

; Initializes disk system
NbloadInitDisk:
    mov [driveNumber], dl
    ret

; Reads a sector from disk
; Input: DX:AX = sector to read, ES:DI = buffer to read into
NbloadReadSector:
    pusha
    ; Set BP to stack top at current moment
    ; This is neccesary for us to read local variables
    push bp
    mov bp, sp
    mov cl, [driveNumber]
    xor ch, ch                      ; Clear upper portion of DX
    push cx                         ; Save it
    ; Save LBA low and high
    push ax                     ; Save sector low
    push dx                     ; Save sector high
%ifdef IS_NOFLOPPY
    ; Attempt to read using LBA BIOS
    ; WARNING: We run extended read, and then if carry is set or AH contains 0x86
    ; or 0x80, we fall back to CHS
    ; If some old BIOS uses AH = 0x42 for some other functions, who knows
    ; what will happen
    ; RBIL doesn't list 42h for anything else, so I think we're OK
    mov si, biosDap
    mov [si+4], di      ; Save destination offset
    mov [si+6], es      ; Save destination segment
    mov [si+8], ax      ; Save LBA low to DAP
    mov [si+10], dx     ; Save LBA high to DAP
    mov dx, [bp-2]      ; Restore drive number
    mov ah, 0x42        ; BIOS extended read
    int 0x13            ; Read it in
    jc .chsBios         ; Carry set? Run CHS function
    cmp ah, 0x86        ; Check for error code
    je .chsBios
    cmp ah, 0x80
    je .chsBios
    jmp .done
%endif
.chsBios:
    ; Now we must convert it to CHS.
    ; This is quite complex, to start, obtain the BIOS'es geometry
    mov dx, [bp-2]      ; Restore drive number
    mov ah, 0x08        ; BIOS get disk geometry
    push es
    push di
    mov bx, 0         ; Set ES and DI to zero to work around buggy BIOSes
    mov es, bx
    mov di, bx
    int 0x13
    pop di
    pop es
    jc .diskError
    and cl, 0x3F        ; Clear upper 2 bits of cylinder to get SPT
    xor ch, ch          ; Clear CH
    push cx             ; Save SPT
    add dh, 1           ; Get HPC
    mov dl, dh          ; Make HPC low byte
    xor dh, dh          ; Clear high byte
    push dx             ; Save HPC
    ; Get LBA / SPT
    mov ax, [bp-4]     ; Place low 16 of LBA in AX
    mov dx, [bp-6]     ; Place upper 16 of LBA in DX
    div cx             ; Get LBA over SPT
    push ax            ; Save LBA / SPT
    push dx            ; Save LBA % SPT
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
    mov cx, 3
.diskRead:
    push cx
    mov dx, [bp-16]
    mov ch, dl          ; Store cylinder low CH for BIOS
    and dh, 3           ; Clear top 6 bits. Cylinder high is in DH
    mov bx, [bp-20]     ; Get sector
    mov cl, bl          ; Stick sector in CL for BIOS
    shl dh, 6           ; Move cylinder high to top two bits of DH
    or cl, dh           ; Stick cylinder high in CL
    mov dx, [bp-18]     ; Get head
    mov dh, dl          ; Stick head in DH
    mov dl, [bp-2]      ; Get drive number in DL
    mov bx, di        ; Offset goes in BX

    mov ah, 2           ; BIOS read sector
    mov al, 1           ; Sector count
    int 0x13            ; Read it in
    jnc .done           ; Did an error occur?
    pop cx
    loop .diskRead
    jmp .diskError
.done:
    mov sp, bp          ; Restore stack pointer
    pop bp
    popa
    ret
.diskError:
    mov si, diskErrMsg
    call BootPrint
    jmp NbloadPanic

; Disk variables
driveNumber: db 0
%ifdef IS_NOFLOPPY
; DAP packet
biosDap:
    db 16
    db 0
    db 1
    db 0
    dw 0          ; Offset to load into
    dw 0          ; Segment to load into
    dq 0         ; Sector to read
%endif
diskErrMsg: db "nbload: disk error", 0

welcomeBanner: db "NexNix version ", NEXNIX_VERSION, 0
