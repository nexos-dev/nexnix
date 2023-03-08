; isombr.asm - contains MBR for ISO9660 disks
; Copyright 2022, 2023 The NexNix Project
;
; Licensed under the Apache License, Version 2.0 (the "License")
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
org 0x7c00

start:
    jmp 0:NbloadMain

times 8 - ($ - $$) db 0
primaryVolDesc: dd 0         ; LBA of PVD
bootFileLba: dd 0            ; LBA of boot file
bootFileLen: dd 0            ; Length of boot file
bootFileCheckum: dd 0        ; Checksum of boot file
times 40 db 0

%include "nbload.inc"
%include "nbload2.inc"

%define NBLOAD_ISO9660_PVDBASE      0x600
%define NBLOAD_ISO9660_SECTSZ       0x800
%define NBLOAD_ISO9660_ROOTDIR_BASE 0x8400

NbloadMain:
    ; Set other segments
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; Set stack and BP
    mov sp, NBLOAD_STACK_TOP
    mov bp, sp
    sti
    call NbloadInitDisk         ; Initialize disk system
    ; Print welcome banner
    mov si, welcomeBanner       ; Get welcome banner
    mov cx, 3                   ; Set log level to info log level
    call NbloadLogMsg
    mov si, welcomeBanner2
    call NbloadLogMsg
    ; Check and make sure we are running on a 386 or higher
    ; How do we do this? First, we rule out if we are on a 8086 / 186
    ; We do this by checking if bit 15 (which is reserved) of FLAGS is 0 or 1.
    ; On the 8086 / 186, it is 1; on 286+, it is 0
    pushf
    pop ax                  ; Get FLAGS in AX
    test ax, 1 << 15        ; Is bit 15 set?
    jnz .no386              ; No 386, panic
    ; We at least are on a 286, verify this is at least a 386
    ; To do this, load a fake 16-bit GDT into GDTR. We get it back with SGDT
    ; Then, we check byte 6. If zeroed, it's a 386. Else, it's a 286.
    mov bx, fakeGdtr
    lgdt [bx]            ; Load in the fake GDT
    sgdt [bx]
    cmp byte [bx+5], 0  ; Is it zeroed still?
    jne .no386          ; No 386, panic

    ; We now need to read a file named nexboot
    ; Print load message first
    mov si, loadMsg
    mov cx, 4
    call NbloadLogMsg
    ; Read in PVD
    mov eax, [primaryVolDesc]
    push ax                            ; Split sector between AX and DX
    shr eax, 16
    mov dx, ax
    pop ax
    mov di, NBLOAD_ISO9660_PVDBASE     ; Set base
    call NbloadReadSector
    ; Store parameters from PVD
    mov ax, [di+128]                    ; Get size of logical block
    push ax                             ; Store that
    ; Convert block size to size in sectors
    xor dx, dx                          ; Prepare DX
    mov cx, NBLOAD_ISO9660_SECTSZ
    div cx                              ; Divide
    ; AX contains size of block in sectors
    push ax                             ; Store it
    ; Read in root directory
    mov eax, [di+166]                   ; Get size of directory
    push eax                            ; Save raw size of directory
    ; Convert size to blocks
    xor ecx, ecx                        ; Clear ECX and EDX for division
    xor edx, edx
    mov cx, [bp-2]                      ; Size of block
    div ecx                             ; Get size of root directory in blocks
    mov ecx, eax                        ; Get size in ECX
    mov eax, [di+158]                   ; Get start block of extent
    mov di, NBLOAD_ISO9660_ROOTDIR_BASE ; Set buffer
    xor dx, dx
    mov es, dx
    call NbloadReadBlocks               ; Read it in
    ; Go looking for a file named "nexboot"
    mov si, di
    add si, 33                          ; Go to file name
.compareLoop:
    ; Compute size of name
    xor cx, cx
    mov cl, [di+32]
    push di
    push si
    mov di, fileName
    rep cmpsb
    je .readFile
    pop si
    pop di
    ; Move to next one
    xor dx, dx
    mov dl, [di]
    add si, dx
    add di, dx  
    cmp byte [di], 0        ; Check if we are at the end
    je .noFile
    jmp .compareLoop
.readFile:
    pop si
    pop di                  ; Restore buffer of directory entry
    ; Ensure this file isn't in interleaved mode
    cmp byte [di+26], 0
    jne .interleaved
    mov ebx, [di+2]         ; Obtain extent
    mov eax, [di+10]        ; Obtain size
    mov [fileSize], eax
    
    ; Round up
    xor ecx, ecx
    mov cx, [bp-2]          ; Get size of a block
    add eax, ecx
    sub eax, 1
    ; Divide size by size of block
    xor edx, edx            ; Prepare for division
    xor ecx, ecx
    mov cx, [bp-2]
    div ecx                 ; Divide by block size
    ; Read it in
    mov cx, ax              ; NOTE: size is truncated!
    mov eax, ebx            ; Get start in EAX
    mov dx, NBLOAD_NEXBOOT_SEG    ; Load segment and offset
    mov es, dx
    mov di, 0
    call NbloadReadBlocks
    ; Re-load drive number
    mov dl, [driveNumber]
    mov ecx, [fileSize]
    jmp NBLOAD_NEXBOOT_SEG:0
.interleaved:
    mov si, interleavedMsg
    call BootPrint
    jmp NbloadPanic
.no386:
    mov si, no386msg
    call BootPrint
    jmp NbloadPanic
.noFile:
    mov si, fileError
    call BootPrint
    jmp NbloadPanic

; Reads several blocks from a CD-ROM
; EAX = block to read, CX = number of blocks to read,
; and ES:DI = buffer to read to
NbloadReadBlocks:
    pusha
.readLoop:
    call NbloadReadBlock
    add eax, 1                  ; Move to next block
    ; Compute size of block
    push eax
    mov ax, [bp-4]              ; Grab number of sectors in a block
    push cx
    mov cx, NBLOAD_ISO9660_SECTSZ
    mul cx                      ; Multiply sector size by sectors in block
    add di, ax                  ; Move to next spot
    pop cx
    pop eax
    loop .readLoop
    popa
    ret

; Reads a single block from CD-ROM
; EAX = block, ES:DI = buffer
NbloadReadBlock:
    pusha
    ; Read in each sector of block, first getting initial sector
    xor ecx, ecx                ; Clear upper part of ECX
    mov cx, [bp-4]              ; Grab number of sectors in a block
    ; Multiply block number by block size to get start sector
    mul ecx                     ; Get start sector
.readLoop2:
    ; Split EAX into DX:AX
    push eax
    shr eax, 16                 ; Get high 16 of sector in AX
    mov dx, ax                  ; Move to DX
    pop eax
    call NbloadReadSector
    add eax, 1                          ; To next sector
    add di, NBLOAD_ISO9660_SECTSZ       ; To next buffer spot
    loop .readLoop2
    ; Print progress
    mov si, progDot
    mov cx, 4
    call NbloadLogMsg
    popa
    ret

; Fake GDTR for 386 detection
fakeGdtr:
    dd 0
    dw 0

; Strings
no386msg: db "nbload: 386+ needed", 0
fileError: db 0x0A, 0x0D, "nbload: unable to read nexboot", 0
interleavedMsg: db 0x0A, 0x0D, "nbload: unable to read interleaved files currently", 0
loadMsg: db 0x0D, 0x0A, "Loading nexboot", 0
progDot: db ".", 0
fileSize: dd 0

; File name we are looking for
fileName: db "nexboot.;1", 0
