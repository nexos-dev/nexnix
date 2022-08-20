# hdvbr.S - contains VBR for hard disks
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

vbrBase:
# The BPB
# NOTE: This BPB is always a FAT32 BPB, as that is the only FS hdvbr supports
# booting from
# NOTE 2: These are NOT the real values. These are simply representing
# the in-memory location of the values
bpbJmp: .byte 0, 0, 0               # 0xEB ?? 0x90
bpbOemName: .ascii "MSWIN4.1"       # BPB junk :)
bpbBytesPerSector: .word 512        # Bytes per sector
bpbSectorsPerClus: .byte 0          # Sectors per cluster
bpbResvdSectors: .word 0            # The number of reserved sector
                                    # The VBR lies here
bpbNumFats: .byte 2                 # Number of FATs
bpbRootEntCount: .word 0            # Unused
bpbTotSector16: .word 0             # Unused
bpbMedia: .byte 0                   # Unused
bpbFatSize16: .word 0               # Unused
bpbSecPerTrack: .word 0             # Unused
bpbNumHeads: .word 0                # Unused
bpbHiddenSectors: .long 0           # Unused
                                    # TODO: Should this be used over
                                    # vbrSector below?
bpbTotSector32: .long 0             # Total number of sectors on disk
bpbFatSize32: .long 0               # FAT size on disk
bpbExtFlags: .word 0                # Flags about FAT mirroring
bpbFsVersion: .word 0               # Version number of FAT, which is 0:0
bpbRootCluster: .long 0             # Cluster of root directory
bpbFsInfoSect: .word 0              # Sector of FSInfo
bpbBackupBoot: .word 0              # Location of backup boot sector
bpbResvd: .long 0, 0, 0
bpbDriveNum: .byte 0                # Unused
bpbResvd1: .byte 0
bpbBootSig: .byte 0                 # Unused
bpbVolumeId: .long 0                # Unused
bpbVolLab: .ascii "           "     # Unused
bpbFileSys: .ascii "FAT32   "       # Unused

# Used to align next value
.word 2
# Sector number of VBR
# nnimage puts this here
vbrSector: .long 0

.global start
start:
    jmp NbloadMain

#include "nbload.inc"

NbloadMain:
    # First, set up a stack
    # Segment registers have been set by the MBR to 0. I hope :)
    mov $NBLOAD_STACK_TOP, %sp
    call NbloadInitDisk         # Initialize disk system
    # Read in NBLOAD sector 2
    mov $vbrSector, %si
    add $LABEL_OFFSET, %si
    mov (%si), %ax
    mov 2(%si), %dx
    add $1, %ax                 # Move to next sector
    mov $NBLOAD_PART2_BASE, %di
    call NbloadReadSector
    # Print welcome banner
    mov $welcomeBanner, %si     # Get welcome banner
    mov $3, %cx                 # Set log level to info log level
    call NbloadLogMsg
    mov $welcomeBanner2, %si
    call NbloadLogMsg
    # Jump to part 2
    jmp NBLOAD_PART2_BASE

# Fake GDTR for 386 detection. We use 6 bytes from the messages for this
fakeGdtr:
no386msg: .asciz "nbload: 386+ needed"
welcomeBanner: .ascii "NexNix version "
.ascii NEXNIX_VERSION
progDot: .asciz "."
fileName: .ascii "NEXBOOT    "
loadMsg: .asciz "\r\nLoading nexboot"

.fill 510 - (. - vbrBase), 1, 0
.word 0xAA55

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
    # To do this, load a fake 16-bit GDT into GDTR. The we get it with SGDT
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
    # is a matter of reading its respective cluster. First, we must get together
    # data neccesary to read a cluster

    # Print loading message
    mov $loadMsg, %si
    mov $4, %cx
    call NbloadLogMsg

    # To read a cluster, we need the first sector in the volume's data area
    # That equals:
    #     vbrSector + bpbResvdSectors + (bpbFatSize32 * bpbNumFats)
    # Load all our data variables
    mov %sp, %bp            # Save frame pointer
    LOAD_DWORD $vbrSector %eax
    xor %ebx, %ebx
    LOAD_WORD $bpbResvdSectors %bx
    add %ebx, %eax          # EAX = base of FAT
    mov %eax, %esi          # Save it
    push %eax               # Save it to frame
    LOAD_DWORD $bpbFatSize32 %eax
    xor %ecx, %ecx
    LOAD_BYTE $bpbNumFats %cl
    mul %ecx                 # Get size of FATs in EAX
    add %eax, %esi          # Get base data sector
    push %esi               # Save to frame
    # We have data sector and FAT base, now we must read in the root directory
    # WARNING: we only read in one cluster. Shouldn't be an issue though
    LOAD_DWORD $bpbRootCluster %eax     # Get root cluster
    mov $NBLOAD_FAT_ROOTDIR_BASE, %di      # Store FAT root directory base
    call NbloadReadCluster
    # We have the root directory. Attempt to find file 'nexboot'
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
    # Compute size of a cluster
    LOAD_WORD $bpbBytesPerSector %ax        # Load bytes per cluster
    xor %cx, %cx                            # Prepare CX
    LOAD_BYTE $bpbSectorsPerClus %cl        # Load sectors per cluster
    mul %cx                                 # Multiply
    mov %ax, %cx                            # Store cluster size
    # Grab cluster low and high
    mov 26(%di), %ax
    mov 20(%di), %dx
    shl $16, %edx           # OR into EAX
    or %edx, %eax
    # Set up buffer
    mov $NBLOAD_NEXBOOT_SEG, %dx
    mov %dx, %es
    mov $0, %di
.readLoop:
    # Read in cluster
    call NbloadReadCluster
    # Figure FAT offset and sector based on cluster number in EAX
    push %cx
    LOAD_WORD $bpbBytesPerSector %cx
    xor %dx, %dx            # Prepare DX for division
    mov $4, %esi
    mul %esi                # Multiply cluster number by 4, as the spec requires
                            # this before computing sector and offset of FAT entry
    div %cx                 # Divide cluster number by bytes per sector.
                            # Quotient = sector number, remainder = offset
    mov %dx, %si            # Store remainder safely in SI
    pop %cx
    # Read in computed FAT sector
    # FIXME: We ought to keep track wheter the current FAT sector has been read yet
    # This would speed up this code a lot
    add -4(%bp), %eax               # Add FAT base
    push %eax                       # Save EAX
    shr $16, %eax                   # Get high 16 of sector
    mov %ax, %dx                    # Get in DX
    pop %eax                        # Restore old AX
    push %es                        # Save output buffer
    push %di
    mov $0, %di
    mov %di, %es                    # Put in buffer of FAT
    mov $NBLOAD_FAT_FAT_BASE, %di
    call NbloadReadSector           # Read it in
    # Now, we must read in this cluster and check for EOF
    add %si, %di                    # Add offset to buffer
    movl (%di), %eax                # Get FAT value of this cluster
    and $0x0FFFFFFF, %eax           # Clear top 4 reserved bits
    cmp $0x0FFFFFF8, %eax           # Check for EOF
    jge .launchNexboot              # If EOF, launch nbdetect
    # Move to next cluster
    pop %di                         # Restore old buffer
    pop %es
    add %cx, %di                    # Move to next location in buffer
    jmp .readLoop                   # Move on
.launchNexboot:
    LOAD_BYTE $driveNumber %dl      # Grab drive number
    ljmp $NBLOAD_NEXBOOT_SEG, $0       # Far jump to nbdetect!
.noNexboot:
    mov $fileError, %si     # Grab error
    call BootPrint          # Print it
    jmp NbloadPanic         # Reboot
.no386:
    mov $no386msg, %si
    call BootPrint
    jmp NbloadPanic

# Strings
fileError: .asciz "\r\nnbload: unable to read nexboot"

# Reads a FAT cluster to memory
# EAX = cluster number, ES:DI = buffer to read into
NbloadReadCluster:
    pusha
    # First sector of cluster equals:
    #    (cluster - 2) + dataSector
    # Data sector is in -8(%bp)
    sub $2, %eax            # Convert to disk cluster number
    addl -8(%bp), %eax      # Add data sector
    # Load sectors per cluster into CX
    LOAD_BYTE $bpbSectorsPerClus %cl  
    xor %ch, %ch
    readLoop:
        push %eax           # Save AX
        shr $16, %eax       # Move high 16 of EAX to DX
        mov %ax, %dx
        pop %eax            # And get low 16 back in AX
        call NbloadReadSector   # Read it in
        add $1, %eax        # Go to next sector
    loop readLoop           # Go to next cluster
    # Print progress dot
    mov $progDot, %si
    mov $4, %cx
    call NbloadLogMsg
    popa
    ret


#include "nbload2.inc"

.fill 512 - (. - start2), 1, 0
