; gptmbr.S - contains MBR for GPT hard disks
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

; This is a horrible hack right here. LLD doesn't allow relocations on the object
; files of flat binaries, hence, we pass -z notext to stop the errors
; The only problem is that is expects up to process the relocations in the .rel.dyn
; section output, which obviously is neither feasible nor even possible here.
; So what do we do? We hack it! Basically, we explicity add LABEL_OFFSET to every label
; reference. That makes it work.
; Note that we have to this with an explicit add instruction, meaning that we must be very
; careful to avoid emitting relocations

; So don't mind the weird syntax of variable references. It's manual position-independent-code.

%ifdef TOOLCHAIN_GNU
%define LABEL_OFFSET 0
%else
%define LABEL_OFFSET 0x600
%endif

; Global constants
%define GPTMBR_BASE 0x600
%define GPTMBR_BIOS_BASE 0x7C00
%define GPTMBR_WORDS 0x100
%define GPTMBR_STACK_TOP 0x7B00

mbrBase:
    jmp start
    nop

; Skip over BPB area, as some BIOSes may trash it
times 87 db 0

; nnimage will stick boot partition VBR base here
resvd: dw 0
bootPartSect: dd 0

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
    mov sp, GPTMBR_STACK_TOP
    mov bp, sp
    ; Relocate us
    mov si, GPTMBR_BIOS_BASE
    mov di, GPTMBR_BASE
    mov cx, GPTMBR_WORDS
    rep movsw
    ; Jump to new position, setting CS in the process
    mov ax, GptMbrEntry
    push 0
    push ax
    retf
GptMbrEntry:
    ; Save DL
    push dx
    sti
    ; gptmbr requires an LBA BIOS. If the user needs to run NexNix 
    ; on a CHS machine, than they can't use GPT disks
    mov ah, 0x41        ; BIOS check LBA extensions
    mov bx, 0x55AA
    int 0x13            ; Call BIOS
    cmp bx, 0xAA55      ; Are they supported?
    jne .noLbaBios      ; Panic
    ; We are certainly on a 386+, so we can now use 32 bit regs
    mov eax, [bootPartSect]         ; Grab boot partition sector
    mov si, biosDap                 ; Grab DAP
    mov [si+8], eax                 ; Store sector in DAP
    mov ah, 0x42                    ; BIOS extended read
    mov dl, [bp-2]                  ; Grab drive number
    int 0x13                        ; Call BIOS
    jc .diskError
    ; Restore DL
    mov dl, [bp-2]
    jmp 0x7C00     ; Off to the VBR we go!
.noLbaBios:
    mov si, noLbaMsg
    call BootPrint
    jmp .reboot
.diskError:
    mov si, diskErrMsg
    call BootPrint
    jmp .reboot
.reboot:
    ; Wait for keypress and reboot
    mov ah, 0
    int 0x16
    int 0x18

biosDap:
    db 0x10
    db 0
    db 1
    db 0
    dw 0x7C00
    dw 0
    dq 0
noLbaMsg: db "gptmbr: LBA BIOS required", 0
diskErrMsg: db "gptmbr: Disk read error", 0

; Pad out to partition table start
times 440 - ($ - $$) db 0
