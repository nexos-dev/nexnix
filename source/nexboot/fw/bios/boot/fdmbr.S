# fdmbr.S - contains MBR for floppy disks
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

mbrBase:
# The BPB
# NOTE: These are NOT the real values. These are simply representing
# the in-memory location of the values
bpbJmp: .byte 0, 0, 0               # 0xEB ?? 0x90
bpbOemName: .ascii "MSWIN4.1"       # BPB junk :)
bpbBytesPerSector: .word 512        # Bytes per sector
bpbSectorsPerClus: .byte 0          # Sectors per cluster
bpbResvdSectors: .word 0            # The number of reserved sector
                                    # The VBR lies here
bpbNumFats: .byte 2                 # Number of FATs
bpbRootEntCount: .word 0            # Count of root directory entries
bpbTotSector16: .word 0             # Unused
bpbMedia: .byte 0                   # Unused
bpbFatSize16: .word 0               # Size of FAT
bpbSecPerTrack: .word 0             # Unused
bpbNumHeads: .word 0                # Unused
bpbHiddenSectors: .long 0           # Unused
bpbTotSector32: .long 0             # Total number of sectors on disk
bpbDriveNum: .byte 0                # Drive number of this
bpbResvd: .byte 0
bpbBootSig: .byte 0x29              # Signature of BPB. This is an MS-DOS 5 BPB
bpbVolumeId: .long 0                # ID of volume
bpbVolumeLab: .ascii "           "  # Volume label
bpbFileSys: .ascii "FAT12   "       # File system type

.global start
start:
    # Setup basic state
    cld
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    # Set CS
    ljmp $0, $NbloadMain

#include "nbload.inc"
#include "nbload2.inc"

NbloadMain:
    # Setup the stack
    mov $NBLOAD_STACK_TOP, %sp
    # Initialize disk system
    call NbloadInitDisk
    # Read in NBLOAD sector 2
    mov $1, %ax
    xor %dx, %dx
    mov $NBLOAD_PART2_BASE, %di
    call NbloadReadSector
    # Print welcome banner
    mov $welcomeBanner, %si     # Get welcome banner
    mov $3, %cx                 # Set log level to info log level
    call NbloadLogMsg
    mov $welcomeBanner2, %si
    call NbloadLogMsg
    # Jump to stage 2
    jmp NBLOAD_PART2_BASE
    cli
    hlt

no386msg: .asciz "nbload: 386+ needed"
progDot: .asciz "."
fileName: .ascii "NEXBOOT    "
loadMsg: .asciz "\r\nLoading nexboot"
.fill 510 - (. - mbrBase), 1, 0
.word 0xAA55

# Second sector of MBR
start2:
    # Check and make sure we are running on a 386 or higher
    # How do we do this? First, we rule out if we are on a 8086 / 186
    # We do this by checking bit 15 (which is reserved) of FLAGS is 0 or 1.
    # On the 8086 / 186, it is 1; on 286+, it is 0
    pushf
    pop %ax         # Get FLAGS in AX
    test $(1 << 15), %ax    # Is bit 15 set?
    jnz .no386              # No 386, panic
    # We at least are on a 286, verify this is at least a 386
    # To do this, load a fake 16-bit GDT into GDTR. Then we get it with SGDT
    # Then, we check byte 6. If zeroed, it's a 386. Else, it's a 286.
    mov $fakeGdtr, %bx
    add $LABEL_OFFSET, %bx
    lgdt (%bx)          # Load in the fake GDT
    sgdt (%bx)
    cmpb $0, 5(%bx)      # Is it zeroed still?
    jne .no386          # No 386, panic
    # We now know we are at least running on a 386!
    # What's next? We now must read the file nexboot.
    # It's in the root directory. Reading in the root directory
    # is a difficult matter. For this reason, we only load one sector
    mov $loadMsg, %si
    mov $4, %cx
    call NbloadLogMsg
    mov %sp, %bp          # Save frame pointer
    # First, compute FAT base
    LOAD_WORD $bpbResvdSectors %ax
    mov %ax, %si          # Save it
    push %ax              # Save it to frame
    LOAD_WORD $bpbFatSize16 %ax
    xor %cx, %cx
    LOAD_BYTE $bpbNumFats %cl
    mul %cx               # Get size of FATs in AX
    add %ax, %si          # Get root directoy sector
    push %si              # Save to frame
    # Get size of root directory
    LOAD_WORD $bpbRootEntCount %ax
    xor %dx, %dx        # Prepare DX for multiplication
    mov $32, %cx
    mul %cx             # Get root directory size (bytes)
    mov $512, %cx
    div %cx             # Get size in sectors
    mov %ax, %dx        # Save size of root directory
    mov -4(%bp), %cx
    add %ax, %cx        # Get data sector
    push %cx
    # Read in root directory
    mov -4(%bp), %ax        # Set base
    mov %dx, %cx            # Set size
    xor %dx, %dx
    mov $NBLOAD_FAT_ROOTDIR_BASE, %di  # Set base of buffer
    push %di
.readDirLoop:
    call NbloadReadSector       # Read it in
    add $512, %di               # Move to next part of buffer
    add $1, %ax                 # Move to next sector
    loop .readDirLoop
    # We have the root directory. Attempt to find file 'nexboot'
    pop %di
.findLoop:
    mov $fileName, %si          # Grab name
    add $LABEL_OFFSET, %si
    mov $11, %cx                # Compare 11 bytes of name
    push %di
    rep cmpsb                   # Compare
    pop %di
    je .readFile                # Equal? Read in file
    cmpb $0, (%di)              # Check if this is the last file
    je .noNexboot               # Go to file not found
    add $32, %di                # Move to next file
    jmp .findLoop
.readFile:
    # Get initial cluster
    mov 26(%di), %ax
    # Setup buffer
    mov $NBLOAD_NEXBOOT_SEG, %cx
    mov %cx, %es
    mov $0, %di
    # Compute size of a cluster
    push %ax
    LOAD_WORD $bpbBytesPerSector %ax        # Load bytes per cluster
    xor %cx, %cx                            # Prepare CX
    LOAD_BYTE $bpbSectorsPerClus %cl        # Load sectors per cluster
    mul %cx                                 # Multiply
    mov %ax, %cx                            # Store cluster size
    pop %ax
.readLoop:
    call NbloadReadCluster
    push %ax                # Save cluster number
    # Determine offset and sector of next FAT entry
    # Multiply cluster by 1.5 without floating point
    push %cx
    mov %ax, %cx
    mov $2, %si
    div %si                 # Divide cluster by 2
    add %ax, %cx            # Add to original to get cluster * 1.5
    mov %cx, %ax
    # Get sector and offset
    LOAD_WORD $bpbBytesPerSector %cx
    xor %dx, %dx
    div %cx                 # Divide cluster number by bytes per sector.
                            # Quotient = sector number, remainder = offset
    
    pop %cx
    mov %dx, %si            # Store offset safely in SI
    # Read in computed FAT sector
    # FIXME: We ought to keep track wheter the current FAT sector has been read yet
    # This would speed up this code a lot
    add -2(%bp), %ax               # Add FAT base
    push %es                       # Save output buffer
    push %di
    mov $0, %di
    mov %di, %es                    # Put in buffer of FAT
    mov $NBLOAD_FAT_FAT_BASE, %di
    xor %dx, %dx                    # Clear sector high
    call NbloadReadSector           # Read it in
    add $1, %ax
    add $0x200, %di
    call NbloadReadSector           # In case we have a sector-boundary cluster
    # Grab entry from FAT
    push %cx
    mov %si, %cx
    mov $NBLOAD_FAT_FAT_BASE, %si   # Grab buffer
    add %cx, %si                    # Move to appropriate entry
    mov (%si), %ax                  # Grab entry
    pop %cx
    # If cluster is even, clear top 4 bits. If odd, shift to remove low 4 bits
    testw $1, -8(%bp)                   # Test it
    jne .odd
.even:
    and $0x0FFF, %ax                # Clear top 4 bits
    jmp .next
.odd:
    shr $4, %ax                     # Remove low 4 bits
.next:
    # Check for EOF
    cmp $0xFF8, %ax
    jge .launchNexboot
    pop %di
    pop %es
    add $2, %sp             # Pop cluster number
    add %cx, %di            # Move to next cluster in memory
    jmp .readLoop
.launchNexboot:
    LOAD_BYTE $driveNumber %dl      # Grab drive number
    ljmp $NBLOAD_NEXBOOT_SEG, $0       # Far jump to nbdetect!
.no386:
    mov $no386msg, %si
    call BootPrint
    jmp NbloadPanic
.noNexboot:
    mov $fileError, %si     # Grab error
    call BootPrint          # Print it
    jmp NbloadPanic         # Reboot

# Reads a FAT cluster to memory
# AX = cluster number, ES:DI = buffer to read into
NbloadReadCluster:
    pusha
    # First sector of cluster equals:
    #    (cluster - 2) + dataSector
    # Data sector is in -8(%bp)
    sub $2, %ax            # Convert to disk cluster number
    addw -6(%bp), %ax      # Add data sector
    # Load sectors per cluster into CX
    LOAD_BYTE $bpbSectorsPerClus %cl  
    xor %ch, %ch
    readLoop:
        xor %dx, %dx        # Clear DX
        call NbloadReadSector   # Read it in
        add $1, %ax        # Go to next sector
    loop readLoop           # Go to next cluster
    # Print progress dot
    mov $progDot, %si
    mov $4, %cx
    call NbloadLogMsg
    popa
    ret

# Strings
fileError: .asciz "\r\nnbload: unable to read nexboot"

# Fake GDTR for 386 detection
fakeGdtr:
    .long 0
    .word 0

.fill 512 - (. - start2), 1, 0
