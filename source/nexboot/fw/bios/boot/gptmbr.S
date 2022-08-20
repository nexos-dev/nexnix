# gptmbr.S - contains MBR for GPT hard disks
# Copyright 2022 The NexNix Project
#
# Licensed under the Apache License, Version 2.0 (the "License")#
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.code16
.section .text

# This is a horrible hack right here. LLD doesn't allow relocations on the object
# files of flat binaries, hence, we pass -z notext to stop the errors
# The only problem is that is expects up to process the relocations in the .rel.dyn
# section output, which obviously is neither feasible nor even possible here.
# So what do we do? We hack it! Basically, we explicity add LABEL_OFFSET to every label
# reference. That makes it work.
# Note that we have to this with an explicit add instruction, meaning that we must be very
# careful to avoid emitting relocations

# So don't mind the weird syntax of variable references. It's manual position-independent-code.

#ifdef TOOLCHAIN_GNU
#define LABEL_OFFSET 0
#else
#define LABEL_OFFSET 0x600
#endif

# Global constants
#define GPTMBR_BASE 0x600
#define GPTMBR_BIOS_BASE 0x7C00
#define GPTMBR_WORDS 0x100
#define GPTMBR_STACK_TOP 0x7B00

mbrBase:
    jmp start

# Skip over BPB area, as some BIOSes may trash it
.skip 87, 0

# nnimage will stick boot partition VBR base here
resvd: .word 0
bootPartSect: .long 0

#include "common.inc"

# Entry point of NexNix
.global start
start:
    # Setup basic state
    cld
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $GPTMBR_STACK_TOP, %sp
    mov %sp, %bp
    # Relocate us
    mov $GPTMBR_BIOS_BASE, %si
    mov $GPTMBR_BASE, %di
    mov $GPTMBR_WORDS, %cx
    rep movsw
    # Jump to new position, setting CS in the process
    mov $LABEL_OFFSET, %ax
    add $GptMbrEntry, %ax
    push $0
    push %ax
    lret
GptMbrEntry:
    # Save DL
    push %dx
    sti
    # gptmbr requires an LBA BIOS. If the user needs to run NexNix 
    # on a CHS machine, than they can't use GPT disks
    mov $0x41, %ah      # BIOS check LBA extensions
    mov $0x55AA, %bx
    int $0x13           # Call BIOS
    cmp $0xAA55, %bx    # Are they supported?
    jne .noLbaBios      # Panic
    # We are certainly on a 386+, so we can now use 32 bit regs
    LOAD_DWORD $bootPartSect %eax       # Grab boot partition sector
    mov $biosDap, %si               # Grab DAP
    add $LABEL_OFFSET, %si
    mov %eax, 8(%si)                # Store sector in DAP
    mov $0x42, %ah                  # BIOS extended read
    mov -2(%bp), %dl                # Grab drive number
    int $0x13                       # Call BIOS
    jc .diskError
    # Restore DL
    mov -2(%bp), %dl
    jmp 0x7C00     # Off to the VBR we go!
    cli
    hlt
.noLbaBios:
    mov $noLbaMsg, %si
    call BootPrint
    jmp .reboot
.diskError:
    mov $diskErrMsg, %si
    call BootPrint
    jmp .reboot
.reboot:
    # Wait for keypress and reboot
    mov $0, %ah
    int $0x16
    int $0x18

biosDap:
    .byte 0x10
    .byte 0
    .byte 1
    .byte 0
    .word 0x7C00
    .word 0
    .quad 0
noLbaMsg: .asciz "gptmbr: LBA BIOS required"
diskErrMsg: .asciz "gptmbr: Disk read error"

# Pad out to partition table start
.skip 440 - (. - mbrBase), 0
