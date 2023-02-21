; fdmbr.S - contains MBR for floppy disks
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
org 0x7C00

; The BPB
; NOTE: These are NOT the real values. These are simply representing
; the in-memory location of the values
bpbJmp: db 0, 0, 0               ; 0xEB ?? 0x90
bpbOemName: db "MSWIN4.1"        ; BPB junk :)
bpbBytesPerSector: dw 512        ; Bytes per sector
bpbSectorsPerClus: db 0          ; Sectors per cluster
bpbResvdSectors: dw 0            ; The number of reserved sector
                                 ; The VBR lies here
bpbNumFats: db 2                 ; Number of FATs
bpbRootEntCount: dw 0            ; Count of root directory entries
bpbTotSector16: dw 0             ; Unused
bpbMedia: db 0                   ; Unused
bpbFatSize16: dw 0               ; Size of FAT
bpbSecPerTrack: dw 0             ; Unused
bpbNumHeads: dw 0                ; Unused
bpbHiddenSectors: dd 0           ; Unused
bpbTotSector32: dd 0             ; Total number of sectors on disk
bpbDriveNum: db 0                ; Drive number of this
bpbResvd: db 0
bpbBootSig: db 0x29              ; Signature of BPB. This is an MS-DOS 5 BPB
bpbVolumeId: dd 0                ; ID of volume
bpbVolumeLab: db "           "   ; Volume label
bpbFileSys: db "FAT12   "        ; File system type

start:
    ; Setup basic state
    cld
    cli
    xor ax, ax
    mov ax, ds
    mov ax, es
    mov ax, ss
    ; Set CS
    jmp 0:NbloadMain

%include "nbload.inc"
%include "nbload2.inc"

NbloadMain:
    ; Setup the stack
    mov sp, NBLOAD_STACK_TOP
    sti
    ; Initialize disk system
    call NbloadInitDisk
    ; Read in NBLOAD sector 2
    mov ax, 1
    xor dx, dx
    mov di, NBLOAD_PART2_BASE
    call NbloadReadSector
    ; Print welcome banner
    mov si, welcomeBanner       ; Get welcome banner
    mov cx, 3                   ; Set log level to info log level
    call NbloadLogMsg
    mov si, welcomeBanner2
    call NbloadLogMsg
    ; Jump to stage 2
    jmp NBLOAD_PART2_BASE

no386msg: db "nbload: 386+ needed", 0
progDot: db ".", 0
fileName: db "NEXBOOT    "
loadMsg: db 0x0D, 0x0A, "Loading nexboot", 0
times 510 - ($ - $$) db 0
dw 0xAA55

; Second sector of MBR
start2:
    ; Check and make sure we are running on a 386 or higher
    ; How do we do this? First, we rule out if we are on a 8086 / 186
    ; We do this by checking bit 15 (which is reserved) of FLAGS is 0 or 1.
    ; On the 8086 / 186, it is 1; on 286+, it is 0
    pushf
    pop ax                  ; Get FLAGS in AX
    test ax, (1 << 15)      ; Is bit 15 set?
    jnz .no386              ; No 386, panic
    ; We at least are on a 286, verify this is at least a 386
    ; To do this, load a fake 16-bit GDT into GDTR. Then we get it with SGDT
    ; Then, we check byte 6. If zeroed, it's a 386. Else, it's a 286.
    mov bx, fakeGdtr
    lgdt [bx]            ; Load in the fake GDT
    sgdt [bx]
    cmp byte [bx+5], 0  ; Is it zeroed still?
    jne .no386          ; No 386, panic
    ; We now know we are at least running on a 386!
    ; What's next? We now must read the file nexboot.
    ; It's in the root directory. Reading in the root directory
    ; is a difficult matter. For this reason, we only load one sector
    mov si, loadMsg
    mov cx, 4
    call NbloadLogMsg
    mov bp, sp            ; Save frame pointer
    ; First, compute FAT base
    mov ax, [bpbResvdSectors]
    mov si, ax            ; Save it
    push ax               ; Save it to frame
    mov ax, [bpbFatSize16]
    xor cx, cx
    mov cl, [bpbNumFats]
    mul cx                ; Get size of FATs in AX
    add si, ax            ; Get root directoy sector
    push si               ; Save to frame
    ; Get size of root directory
    mov ax, [bpbRootEntCount]
    xor dx, dx          ; Prepare DX for multiplication
    mov cx, 32
    mul cx              ; Get root directory size (bytes)
    mov cx, 512
    div cx              ; Get size in sectors
    mov dx, ax          ; Save size of root directory
    mov cx, [bp-4]
    add cx, ax          ; Get data sector
    push cx
    ; Read in root directory
    mov ax, [bp-4]       ; Set base
    mov cx, dx           ; Set size
    xor dx, dx
    mov di, NBLOAD_FAT_ROOTDIR_BASE  ; Set base of buffer
    push di
.readDirLoop:
    call NbloadReadSector       ; Read it in
    add di, 512                 ; Move to next part of buffer
    add ax, 1                   ; Move to next sector
    loop .readDirLoop
    ; We have the root directory. Attempt to find file 'nexboot'
    pop di
.findLoop:
    mov si, fileName            ; Grab name
    mov cx, 11                  ; Compare 11 bytes of name
    push di
    rep cmpsb                   ; Compare
    pop di
    je .readFile                ; Equal? Read in file
    cmp byte [di], 0            ; Check if this is the last file
    je .noNexboot               ; Go to file not found
    add di, 32                  ; Move to next file
    jmp .findLoop
.readFile:
    ; Get initial cluster
    mov ax, [di+26]
    ; Setup buffer
    mov cx, NBLOAD_NEXBOOT_SEG
    mov es, cx
    mov di, 0
    ; Compute size of a cluster
    push ax
    mov ax, [bpbBytesPerSector]             ; Load bytes per cluster
    xor cx, cx                              ; Prepare CX
    mov cl, [bpbSectorsPerClus]             ; Load sectors per cluster
    mul cx                                  ; Multiply
    mov cx, ax                              ; Store cluster size
    pop ax
.readLoop:
    call NbloadReadCluster
    push ax                 ; Save cluster number
    ; Determine offset and sector of next FAT entry
    ; Multiply cluster by 1.5 without floating point
    push cx
    mov cx, ax
    mov si, 2
    div si                  ; Divide cluster by 2
    add cx, ax              ; Add to original to get cluster * 1.5
    mov ax, cx
    ; Get sector and offset
    mov cx, [bpbBytesPerSector]
    xor dx, dx
    div cx                  ; Divide cluster number by bytes per sector.
                            ; Quotient = sector number, remainder = offset
    
    pop cx
    mov si, dx              ; Store offset safely in SI
    ; Read in computed FAT sector
    ; FIXME: We ought to keep track wheter the current FAT sector has been read yet
    ; This would speed up this code a lot
    add ax, [bp-2]                 ; Add FAT base
    push es                        ; Save output buffer
    push di
    mov di, 0
    mov es, di                     ; Put in buffer of FAT
    mov di, NBLOAD_FAT_FAT_BASE
    xor dx, dx                      ; Clear sector high
    call NbloadReadSector           ; Read it in
    add ax, 1
    add di, 0x200
    call NbloadReadSector           ; In case we have a sector-boundary cluster
    ; Grab entry from FAT
    push cx
    mov cx, si
    mov si, NBLOAD_FAT_FAT_BASE     ; Grab buffer
    add si, cx                      ; Move to appropriate entry
    mov ax, [si]                    ; Grab entry
    pop cx
    ; If cluster is even, clear top 4 bits. If odd, shift to remove low 4 bits
    test word [bp-8], 1             ; Test it
    jne .odd
.even:
    and ax, 0x0FFF                  ; Clear top 4 bits
    jmp .next
.odd:
    shr ax, 4                       ; Remove low 4 bits
.next:
    ; Check for EOF
    cmp ax, 0xFF8
    jae .launchNexboot
    pop di
    pop es
    add sp, 2               ; Pop cluster number
    add di, cx              ; Move to next cluster in memory
    jmp .readLoop
.launchNexboot:
    mov dl, [driveNumber]
    jmp NBLOAD_NEXBOOT_SEG:0 ; Far jump to nbload!
.no386:
    mov si, no386msg
    call BootPrint
    jmp NbloadPanic
.noNexboot:
    mov si, fileError       ; Grab error
    call BootPrint          ; Print it
    jmp NbloadPanic         ; Reboot

; Reads a FAT cluster to memory
; AX = cluster number, ES:DI = buffer to read into
NbloadReadCluster:
    pusha
    ; First sector of cluster equals:
    ;    (cluster - 2) + dataSector
    ; Data sector is in -8(%bp)
    sub ax, 2              ; Convert to disk cluster number
    add ax, word [bp-6]    ; Add data sector
    ; Load sectors per cluster into CX
    mov cl, [bpbSectorsPerClus]
    xor ch, ch
    readLoop:
        xor dx, dx         ; Clear DX
        call NbloadReadSector   ; Read it in
        add ax, 1          ; Go to next sector
    loop readLoop          ; Go to next cluster
    ; Print progress dot
    mov si, progDot
    mov cx, 4
    call NbloadLogMsg
    popa
    ret

; Strings
fileError: db 0x0D, 0x0A, "nbload: unable to read nexboot", 0

; Fake GDTR for 386 detection
fakeGdtr:
    dd 0
    dw 0

times 1024 - ($ - $$) db 0
