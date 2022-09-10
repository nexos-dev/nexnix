# isombr.S - contains MBR for ISO9660 disks
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

.global start
start:
    ljmp $0, $NbloadMain

.fill 8 - (. - start), 1, 0
primaryVolDesc: .long 0         # LBA of PVD
bootFileLba: .long 0            # LBA of boot file
bootFileLen: .long 0            # Length of boot file
bootFileCheckum: .long 0        # Checksum of boot file
.fill 40, 1, 0

#include "nbload.inc"
#include "nbload2.inc"

#define NBLOAD_ISO9660_PVDBASE      0x600
#define NBLOAD_ISO9660_SECTSZ       0x800
#define NBLOAD_ISO9660_ROOTDIR_BASE 0x8400

NbloadMain:
    # Set other segments
    mov $0, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    # Set stack and BP
    mov $NBLOAD_STACK_TOP, %sp
    mov %sp, %bp
    #sti
    call NbloadInitDisk         # Initialize disk system
    # Print welcome banner
    mov $welcomeBanner, %si     # Get welcome banner
    mov $3, %cx                 # Set log level to info log level
    call NbloadLogMsg
    mov $welcomeBanner2, %si
    call NbloadLogMsg
    # Check and make sure we are running on a 386 or higher
    # How do we do this? First, we rule out if we are on a 8086 / 186
    # We do this by checking if bit 15 (which is reserved) of FLAGS is 0 or 1.
    # On the 8086 / 186, it is 1; on 286+, it is 0
    pushf
    pop %ax         # Get FLAGS in AX
    test $(1 << 15), %ax    # Is bit 15 set?
    jnz .no386              # No 386, panic
    # We at least are on a 286, verify this is at least a 386
    # To do this, load a fake 16-bit GDT into GDTR. We get it back with SGDT
    # Then, we check byte 6. If zeroed, it's a 386. Else, it's a 286.
    mov $fakeGdtr, %bx
    add $LABEL_OFFSET, %bx
    lgdt (%bx)          # Load in the fake GDT
    sgdt (%bx)
    cmpb $0, 5(%bx)      # Is it zeroed still?
    jne .no386          # No 386, panic

    # We now need to read a file named nexboot
    # Print load message first
    mov $loadMsg, %si
    mov $4, %cx
    call NbloadLogMsg
    # Read in PVD
    LOAD_DWORD $primaryVolDesc %eax
    push %ax                # Split sector between AX and DX
    shr $16, %eax
    mov %ax, %dx
    pop %ax
    mov $NBLOAD_ISO9660_PVDBASE, %di   # Set base
    call NbloadReadSector
    # Store parameters from PVD
    mov 128(%di), %ax                   # Get size of logical block
    push %ax                            # Store that
    # Convert block size to size in sectors
    xor %dx, %dx                        # Prepare DX
    mov $NBLOAD_ISO9660_SECTSZ, %cx
    div %cx                             # Divide
    # AX contains size of block in sectors
    push %ax                            # Store it
    # Read in root directory
    mov 166(%di), %eax                  # Get size of directory
    push %eax                           # Save raw size of directory
    # Convert size to blocks
    xor %ecx, %ecx                      # Clear ECX and EDX for division
    xor %edx, %edx
    movw -2(%bp), %cx                   # Size of block
    div %ecx                            # Get size of root directory in blocks
    mov %eax, %ecx                      # Get size in ECX
    mov 158(%di), %eax                  # Get start block of extent
    mov $NBLOAD_ISO9660_ROOTDIR_BASE, %di   # Set buffer
    xor %dx, %dx
    mov %dx, %es
    call NbloadReadBlocks               # Read it in
    # Go looking for a file named "nexboot"
    mov %di, %si
    add $33, %si                        # Go to file name
.compareLoop:
    # Compute size of name
    movzxb 32(%di), %cx
    push %di
    push %si
    mov $fileName, %di
    rep cmpsb
    je .readFile
    pop %si
    pop %di
    # Move to next one
    movzxb (%di), %dx
    add %dx, %si
    add %dx, %di  
    cmpb $0, (%di)           # Check if we are at the end
    je .noFile
    jmp .compareLoop
.readFile:
    pop %si
    pop %di                 # Restore buffer of directory entry
    # Ensure this file isn't in interleaved mode
    cmpb $0, 26(%di)
    jne .interleaved
    mov 2(%di), %ebx        # Obtain extent
    mov 10(%di), %eax       # Obtain size
    # Round up
    movzxw -2(%bp), %ecx    # Get size of a block
    add %ecx, %eax
    sub $1, %eax
    # Divide size by size of block
    xor %edx, %edx          # Prepare for division
    movzxw -2(%bp), %ecx
    div %ecx                # Divide by block size
    # Read it in
    mov %ax, %cx            # NOTE: size is truncated!
    mov %ebx, %eax          # Get start in EAX
    mov $NBLOAD_NEXBOOT_SEG, %dx    # Load segment and offset
    mov %dx, %es
    mov $0, %di
    call NbloadReadBlocks
    # Re-load drive number
    LOAD_BYTE $driveNumber %dl
    ljmp $NBLOAD_NEXBOOT_SEG, $0
.interleaved:
    mov $interleavedMsg, %si
    call BootPrint
    jmp NbloadPanic
.no386:
    mov $no386msg, %si
    call BootPrint
    jmp NbloadPanic
.noFile:
    mov $fileError, %si
    call BootPrint
    jmp NbloadPanic

# Reads several blocks from a CD-ROM
# EAX = block to read, CX = number of blocks to read,
# and ES:DI = buffer to read to
NbloadReadBlocks:
    pusha
.readLoop:
    call NbloadReadBlock
    add $1, %eax                # Move to next block
    # Compute size of block
    push %eax
    mov -4(%bp), %ax            # Grab number of sectors in a block
    push %cx
    mov $NBLOAD_ISO9660_SECTSZ, %cx
    mul %cx                     # Multiply sector size by sectors in block
    add %ax, %di                # Move to next spot
    pop %cx
    pop %eax
    loop .readLoop
    popa
    ret

# Reads a single block from CD-ROM
# EAX = block, ES:DI = buffer
NbloadReadBlock:
    pusha
    # Read in each sector of block, first getting initial sector
    xor %ecx, %ecx              # Clear upper part of ECX
    mov -4(%bp), %cx            # Grab number of sectors in a block
    # Multiply block number by block size to get start sector
    mul %ecx                    # Get start sector
.readLoop2:
    # Split EAX into DX:AX
    push %eax
    shr $16, %eax               # Get high 16 of sector in AX
    mov %ax, %dx                # Move to DX
    pop %eax
    call NbloadReadSector
    add $1, %eax                        # To next sector
    add $NBLOAD_ISO9660_SECTSZ, %di     # To next buffer spot
    loop .readLoop2
    # Print progress
    mov $progDot, %si
    mov $4, %cx
    call NbloadLogMsg
    popa
    ret

# Fake GDTR for 386 detection
fakeGdtr:
    .long 0
    .word 0

# Strings
no386msg: .asciz "nbload: 386+ needed"
fileError: .asciz "\r\nnbload: unable to read nexboot"
interleavedMsg: .asciz "\r\nnbload: unable to read interleaved files currently"
loadMsg: .asciz "\r\nLoading nexboot"
progDot: .asciz "."

# File name we are looking for
fileName: .asciz "nexboot.;1"
