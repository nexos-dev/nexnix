# hdmbr.S - contains MBR for hard disks
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
#define HDMBR_BASE 0x600
#define HDMBR_BIOS_BASE 0x7C00
#define HDMBR_WORDS 0x100
#define HDMBR_STACK_TOP 0x7B00
#define HDMBR_PARTTAB_START 0x7BE
#define HDMBR_ACTIVE (1 << 7)
#define HDMBR_ENTRY_SIZE 16
#define HDMBR_VBR_BASE 0x7C00
#define HDMBR_VBR_SIG 0x7DFE
#define HDMBR_BIOS_SIG 0xAA55

mbrBase:
    jmp start
    nop
# Some BIOSes expect a valid BPB, and will trash the MBR if one isn't found.
# Account for that
.skip 87, 0

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
    mov $HDMBR_STACK_TOP, %sp
    mov %sp, %bp
    # Relocate us
    mov $HDMBR_BIOS_BASE, %si
    mov $HDMBR_BASE, %di
    mov $HDMBR_WORDS, %cx
    rep movsw
    # Jump to new position, setting CS in the process
    mov $LABEL_OFFSET, %ax
    add $HdMbrEntry, %ax
    push $0
    push %ax
    lret
HdMbrEntry:
    # Save DL
    push %dx
    sti
    # Find active partition
    mov $HDMBR_PARTTAB_START, %si
    mov $4, %cx
.activeLoop:
    movb (%si), %al
    test $HDMBR_ACTIVE, %al
    jnz .activeFound
    add $HDMBR_ENTRY_SIZE, %si
    loop .activeLoop
    # Print error, as there is no active partition
    mov $msgPrefix, %si
    call BootPrint
    mov $bootPartErr, %si
    call BootPrint
    jmp .reboot
.activeFound:
    # Get partition start
    movw 8(%si), %ax
    push %ax            # Save sector low
    movw 10(%si), %bx
    push %bx            # Save sector high
    # Attempt to read using LBA BIOS
    # WARNING: We run extended read, and then if carry is set or AH contains 0x86
    # or 0x80, we fall back to CHS
    # If some old BIOS uses AH = 0x42 for some other functions, who knows
    # what will happen
    # RBIL doesn't list 42h for anything else, so I think we're OK
    mov $dap, %si
    add $LABEL_OFFSET, %si
    mov %ax, 8(%si)     # Save LBA low to DAP
    mov %bx, 10(%si)     # Save LBA high to DAP
    mov -2(%bp), %dx    # Restore drive number
    mov $0x42, %ah      # BIOS extended read
    int $0x13           # Read it in
    jc .chsBios         # Carry set? Run CHS function
    cmp $0x86, %ah      # Check for error code
    je .chsBios
    cmp $0x80, %ah
    je .chsBios
    jmp .launchVbr
.chsBios:
    # Now we must convert it to CHS.
    # This is quite complex, to start, obtain the BIOS'es geometry
    mov -2(%bp), %dx     # Restore drive number
    mov $0x08, %ah      # BIOS get disk geometry
    int $0x13
    jc .diskError
    and $0x3F, %cl      # Clear upper 2 bits of cylinder to get SPT
    xor %ch, %ch        # Clear CH
    push %cx            # Save SPT
    add $1, %dh         # Get HPC
    mov %dh, %dl        # Make HPC low byte
    xor %dh, %dh        # Clear high byte
    push %dx            # Save HPC
    # Get LBA / SPT
    mov -4(%bp), %ax        # Place low 16 of LBA in AX
    mov -6(%bp), %dx        # Place upper 16 of LBA in DX
    div %cx             # Get LBA over SPT
    push %ax            # Save LBA / SPT
    push %dx            # Save LBA % SPT
    # Get cylinder and head 
    xor %dx, %dx        # Clear DX
    divw -10(%bp)        # Get them
    push %ax            # Save cylinder
    push %dx            # Save head
    # Get sector
    addw $1, -14(%bp)   # Add 1 to LBA % SPT
    mov -14(%bp), %ax
    push %ax            # Re-push it
    # Call BIOS to read in sector now
    # Reset disk system
    mov -16(%bp), %dx
    mov %dl, %ch        # Store cylinder low CH for BIOS
    and $0x3, %dh       # Clear top 6 bits. Cylinder high is in DH
    mov -20(%bp), %bx   # Get sector
    mov %bl, %cl        # Stick sector in CL for BIOS
    shl $6, %dh         # Move cylinder high to top two bits of DH
    and $0xC0, %dh
    or %dh, %cl         # Stick cylinder high in CL
    mov -18(%bp), %dx   # Get head
    mov %dl, %dh        # Stick head in DH
    mov -2(%bp), %dl    # Get drive number in DL
    mov $HDMBR_VBR_BASE, %bx    # VBR base goes in BX
    xor %si, %si
    mov %si, %es
    mov $2, %ah         # BIOS read sector
    mov $1, %al         # Sector count
    int $0x13           # Read it in
    jc .diskError       # Did an error occur?
.launchVbr:
    # VBR is now read, check signature
    cmpw $HDMBR_BIOS_SIG, HDMBR_VBR_SIG
    jne .invalidSig
    # Restore DL
    mov -2(%bp), %dx
    # Off to VBR land we go!
    jmp HDMBR_VBR_BASE
.diskError:
    mov $msgPrefix, %si
    call BootPrint
    mov $diskErrMsg, %si
    call BootPrint
    jmp .reboot
.invalidSig:
    mov $msgPrefix, %si
    call BootPrint
    mov $sigMsg, %si
    call BootPrint
    jmp .reboot
.reboot:
    # Wait for key strike and reboot
    mov $0, %ah
    int $0x16
    int $0x18

dap:
    .byte 0x10
    .byte 0
    .byte 1
    .byte 0
    .word 0x7C00
    .word 0
    .quad 0
msgPrefix: .asciz "hdmbr: "
bootPartErr: .asciz "No boot partition"
diskErrMsg: .asciz "Disk error"
sigMsg: .asciz "Invalid VBR signature"

# Pad out to partition table start
.fill 440 - (. - mbrBase), 1, 0
