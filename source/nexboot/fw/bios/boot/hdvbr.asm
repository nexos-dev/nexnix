; hdvbr.S - contains VBR for hard disks
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
org 0x7C00

; The BPB
; NOTE: This BPB is always a FAT32 BPB, as that is the only FS hdvbr supports
; booting from
; NOTE 2: These are NOT the real values. These are simply representing
; the in-memory location of the values
bpbJmp: db 0, 0, 0                  ; 0xEB ?? 0x90
bpbOemName: db "MSWIN4.1"           ; BPB junk :)
bpbBytesPerSector: dw 512           ; Bytes per sector
bpbSectorsPerClus: db 0             ; Sectors per cluster
bpbResvdSectors: dw 0               ; The number of reserved sector
                                    ; The VBR lies here
bpbNumFats: db 2                    ; Number of FATs
bpbRootEntCount: dw 0               ; Unused
bpbTotSector16: dw 0                ; Unused
bpbMedia: db 0                      ; Unused
bpbFatSize16: dw 0                  ; Unused
bpbSecPerTrack: dw 0                ; Unused
bpbNumHeads: dw 0                   ; Unused
bpbHiddenSectors: dd 0              ; Unused
                                    ; TODO: Should this be used over
                                    ; vbrSector below?
bpbTotSector32: dd 0                ; Total number of sectors on disk
bpbFatSize32: dd 0                  ; FAT size on disk
bpbExtFlags: dw 0                   ; Flags about FAT mirroring
bpbFsVersion: dw 0                  ; Version number of FAT, which is 0:0
bpbRootCluster: dd 0                ; Cluster of root directory
bpbFsInfoSect: dw 0                 ; Sector of FSInfo
bpbBackupBoot: dw 0                 ; Location of backup boot sector
bpbResvd: dd 0, 0, 0
bpbDriveNum: db 0                   ; Unused
bpbResvd1: db 0
bpbBootSig: db 0                    ; Unused
bpbVolumeId: dd 0                   ; Unused
bpbVolLab: db "           "         ; Unused
bpbFileSys: db "FAT32   "           ; Unused

; Used to align next value
dw 2
; Sector number of VBR
; nnimage puts this here
vbrSector: dd 0

start:
    jmp NbloadMain

%include "nbload.inc"

NbloadMain:
    ; First, set up a stack
    ; Segment registers have been set by the MBR to 0. I hope :)
    mov sp, NBLOAD_STACK_TOP
    sti
    call NbloadInitDisk         ; Initialize disk system
    ; Read in NBLOAD sector 2
    mov si, vbrSector
    mov ax, [si]
    mov dx, [si+2]
    add ax, 1                   ; Move to next sector
    mov di, NBLOAD_PART2_BASE
    call NbloadReadSector
    ; Print welcome banner
    mov si, welcomeBanner       ; Get welcome banner
    mov cx, 3                   ; Set log level to info log level
    call NbloadLogMsg
    mov si, welcomeBanner2
    call NbloadLogMsg
    ; Jump to part 2
    jmp NBLOAD_PART2_BASE

no386msg: db "nbload: 386+ needed", 0
progDot: db ".", 0
loadMsg: db 0x0A, 0x0D, "Loading nexboot", 0

times 510 - ($ - $$) db 0
dw 0xAA55

start2:
    ; Check and make sure we are running on a 386 or higher
    ; How do we do this? First, we rule out if we are on a 8086 / 186
    ; We do this by checking bit 15 (which is reserved) of FLAGS is 0 or 1.
    ; On the 8086 / 186, it is 1; on 286+, it is 0
    pushf
    pop ax                  ; Get FLAGS in AX
    test ax, 1 << 15        ; Is bit 15 set?
    jnz .no386              ; No 386, panic
    ; We at least are on a 286, verify this is at least a 386
    ; To do this, load a fake 16-bit GDT into GDTR. The we get it with SGDT
    ; Then, we check byte 6. If zeroed, it's a 386. Else, it's a 286.
    mov bx, fakeGdtr
    lgdt [bx]             ; Load in the fake GDT
    sgdt [bx]
    cmp byte [bx+5], 0    ; Is it zeroed still?
    jne .no386            ; No 386, panic
    ; We now know we are at least running on a 386!
    ; What's next? We now must read the file nexboot.
    ; It's in the root directory. Reading in the root directory
    ; is a matter of reading its respective cluster. First, we must get together
    ; data neccesary to read a cluster

    ; Print loading message
    mov si, loadMsg
    mov cx, 4
    call NbloadLogMsg

    ; To read a cluster, we need the first sector in the volume's data area
    ; That equals:
    ;     vbrSector + bpbResvdSectors + (bpbFatSize32 * bpbNumFats)
    ; Load all our data variables
    mov bp, sp              ; Save frame pointer
    mov eax, [vbrSector]
    xor ebx, ebx
    mov bx, [bpbResvdSectors]
    add eax, ebx            ; EAX = base of FAT
    mov esi, eax            ; Save it
    push eax                ; Save it to frame
    mov eax, [bpbFatSize32]
    xor ecx, ecx
    mov cl, [bpbNumFats]
    mul ecx                 ; Get size of FATs in EAX
    add esi, eax            ; Get base data sector
    push esi                ; Save to frame
    ; We have data sector and FAT base, now we must read in the root directory
    ; WARNING: we only read in one cluster. Shouldn't be an issue though
    mov eax, [bpbRootCluster]              ; Get root cluster
    mov di, NBLOAD_FAT_ROOTDIR_BASE        ; Store FAT root directory base
    call NbloadReadCluster
    ; We have the root directory. Attempt to find file 'nexboot'
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
    ; Compute size of a cluster
    mov ax, [bpbBytesPerSector]          ; Load bytes per sector
    xor cx, cx                           ; Prepare CX
    mov cl, [bpbSectorsPerClus]          ; Load sectors per cluster
    mul cx                               ; Multiply
    mov cx, ax                           ; Store cluster size
    ; Grab cluster low and high
    xor eax, eax
    xor edx, edx
    mov ax, [di+26]
    mov dx, [di+20]
    shl edx, 16           ; OR into EAX
    or eax, edx
    ; Set up buffer
    mov dx, NBLOAD_NEXBOOT_SEG
    mov es, dx
    mov di, 0
.readLoop:
    ; Read in cluster
    call NbloadReadCluster
    ; Figure FAT offset and sector based on cluster number in EAX
    push cx
    mov cx, [bpbBytesPerSector]
    xor dx, dx              ; Prepare DX for division
    mov esi, 4
    mul esi                 ; Multiply cluster number by 4, as the spec requires
                            ; this before computing sector and offset of FAT entry
    div cx                  ; Divide cluster number by bytes per sector.
                            ; Quotient = sector number, remainder = offset
    mov si, dx              ; Store remainder safely in SI
    pop cx
    ; Read in computed FAT sector
    ; FIXME: We ought to keep track wheter the current FAT sector has been read yet
    ; This would speed up this code a lot
    add eax, [bp-4]                 ; Add FAT base
    push eax                        ; Save EAX
    shr eax, 16                     ; Get high 16 of sector
    mov dx, ax                      ; Get in DX
    pop eax                         ; Restore old AX
    push es                         ; Save output buffer
    push di
    mov di, 0
    mov es, di                      ; Put in buffer of FAT
    mov di, NBLOAD_FAT_FAT_BASE
    call NbloadReadSector           ; Read it in
    ; Now, we must read in this cluster and check for EOF
    add di, si                      ; Add offset to buffer
    mov eax, [di]                   ; Get FAT value of this cluster
    and eax, 0x0FFFFFFF             ; Clear top 4 reserved bits
    cmp eax, 0x0FFFFFF8             ; Check for EOF
    jae .launchNexboot              ; If EOF, launch nbload
    ; Move to next cluster
    pop di                          ; Restore old buffer
    pop es
    add di, cx                      ; Move to next location in buffer
    jmp .readLoop                   ; Move on
.launchNexboot:
    mov dl, [driveNumber]           ; Grab drive number
    jmp NBLOAD_NEXBOOT_SEG:0        ; Far jump to nbload!
.noNexboot:
    mov si, fileError       ; Grab error
    call BootPrint          ; Print it
    jmp NbloadPanic         ; Reboot
.no386:
    mov si, no386msg
    call BootPrint
    jmp NbloadPanic

; Strings
fileError: db 0x0A, 0x0D, "nbload: unable to read nexboot", 0

; Reads a FAT cluster to memory
; EAX = cluster number, ES:DI = buffer to read into
NbloadReadCluster:
    pusha
    ; First sector of cluster equals:
    ;    (cluster - 2) + dataSector
    ; Data sector is in bp-8
    sub eax, 2              ; Convert to disk cluster number
    add eax, [bp-8]        ; Add data sector
    ; Load sectors per cluster into CX
    mov cl, [bpbSectorsPerClus]
    xor ch, ch
    readLoop:
        push eax            ; Save AX
        shr eax, 16         ; Move high 16 of EAX to DX
        mov dx, ax
        pop eax             ; And get low 16 back in AX
        call NbloadReadSector   ; Read it in
        add eax, 1          ; Go to next sector
    loop readLoop           ; Go to next cluster
    ; Print progress dot
    mov si, progDot
    mov cx, 4
    call NbloadLogMsg
    popa
    ret

; Fake GDTR for 386 detection
fakeGdtr:
    dd 0
    dw 0

fileName: db "NEXBOOT    "
%include "nbload2.inc"

times 1024 - ($ - $$) db 0
