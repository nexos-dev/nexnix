# nbload.inc - contains common code for stage 1 / 1.5
# Copyright 2022, 2023 The NexNix Project
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

# This is a horrible hack right here. LLD doesn't allow relocations on the object
# files of flat binaries, hence, we pass -z notext to stop the errors
# The only problem is that is expects up to process the relocations in the .rel.dyn
# section output, which obviously is neither feasible nor even possible here.
# So what do we do? We hack it! Basically, we explicity add LABEL_OFFSET to every label
# reference. That makes it work.
# Note that we have to this with an explicit add instruction, meaning that we ust be very
# careful to avoid emitting relocations

# So don't mind the weird syntax of variable references. It's manual position-independent-code.

#ifdef TOOLCHAIN_GNU
#define LABEL_OFFSET 0
#else
#define LABEL_OFFSET 0x7C00
#endif

#include "common.inc"
#include <version.h>

# Memory layout
# Address 0x0 - 0x5FF is reserved. Note that 0x500 - 0x5FF _could_ be used, but some
# old BIOSes use it for PrtScr status
# Address 0x600 - 0x7C00 are used for our purposes. We reserve a 4 KiB stack from 0x7C00
# down to 0x6C00. 
# Everything beneath there is used for whatever we need
# Note that we don't have an official memory allocation system; we hard code what we need
# to specific addresses
#define NBLOAD_STACK_TOP 0x7C00
#define NBLOAD_FREEMEM_BASE 0x600
#define NBLOAD_FREEMEM_TOP 0x6BFF
#define NBLOAD_PART2_BASE 0x7E00
#define NBLOAD_FAT_ROOTDIR_BASE 0x8000
#define NBLOAD_FAT_FAT_BASE 0x800
#define NBLOAD_NEXBOOT_SEG 0x1000

# Waits for key strike and goes to next boot device
NbloadPanic:
    mov $0, %ah
    int $0x16
    int $0x18

#define NBLOAD_LOG_START 0x600
#define NBLOAD_LOG_SIZE 0x100

# Logs a message to boot log
# Input: DS:SI = string to write, CX = log level of message
NbloadLogMsg:
    pusha
    # Write message out to log
    mov %si, %bx                # Save SI
    push %bx                    # BX is trashed by LOAD/STORE_WORD
    LOAD_WORD $logLocation, %si # Get log location
    pop %bx
    add $LABEL_OFFSET, %bx
    # Format of log entry:
    # Offset 0 - 1: Message offset
    # Offset 2 - 3: Message segment
    # Offset 4 - 5: Log level
    mov %bx, (%si)              # Write message offset
    mov %ds, 2(%si)             # Write segment
    mov %cx, 4(%si)             # Write log level
    # Write out new log location
    add $6, %si                 # Move to next log entry
    push %bx
    STORE_WORD %si $logLocation # Store it
    pop %bx
    mov %bx, %si                # Get message back in SI
    # Decide where to print
    cmp $NEXNIX_LOGLEVEL, %cx
    jg .logDone                    # Print to serial port if message level
                                # is greater than system level
    sub $LABEL_OFFSET, %si      # Remove label offset, BootPrint puts it back in
    call BootPrint              # Print to screen if allowed, then to serial port
.logDone:
    popa
    ret
# The current log pointer
logLocation: .word NBLOAD_LOG_START

# Initializes disk system
NbloadInitDisk:
    push %bx
    # Save drive number
    STORE_BYTE %dl $driveNumber
    pop %bx
    ret

# Reads a sector from disk
# Input: DX:AX = sector to read, ES:DI = buffer to read into
NbloadReadSector:
#ifndef IS_FLOPPY
    pusha
    # Set BP to stack top at current moment
    # This is neccesary for us to read local variables
    mov %sp, %bp
    LOAD_BYTE $driveNumber %cl      # Get drive number
    xor %ch, %ch                    # Clear upper portion of DX
    push %cx                        # Save it
    # Save LBA low and high
    push %ax                    # Save sector low
    push %dx                    # Save sector high
    # Attempt to read using LBA BIOS
    # WARNING: We run extended read, and then if carry is set or AH contains 0x86
    # or 0x80, we fall back to CHS
    # If some old BIOS uses AH = 0x42 for some other functions, who knows
    # what will happen
    # RBIL doesn't list 42h for anything else, so I think we're OK
    mov $biosDap, %si
    add $LABEL_OFFSET, %si
    mov %di, 4(%si)     # Save destination offset
    mov %es, 6(%si)     # Save destination segment
    mov %ax, 8(%si)     # Save LBA low to DAP
    mov %dx, 10(%si)     # Save LBA high to DAP
    mov -2(%bp), %dx    # Restore drive number
    mov $0x42, %ah      # BIOS extended read
    int $0x13           # Read it in
    jc .chsBios         # Carry set? Run CHS function
    cmp $0x86, %ah      # Check for error code
    je .chsBios
    cmp $0x80, %ah
    je .chsBios
    jmp .done
.chsBios:
    # Now we must convert it to CHS.
    # This is quite complex, to start, obtain the BIOS'es geometry
    mov -2(%bp), %dx     # Restore drive number
    mov $0x08, %ah      # BIOS get disk geometry
    push %es
    push %di
    mov $0, %bx         # Set ES and DI to zero to work around buggy BIOSes
    mov %bx, %es
    mov %bx, %di
    int $0x13
    jc .diskError
    pop %di
    pop %es
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
    mov %di, %bx        # Offset goes in BX

    mov $2, %ah         # BIOS read sector
    mov $1, %al         # Sector count
    int $0x13           # Read it in
    jc .diskError       # Did an error occur?
.done:
    mov %bp, %sp        # Restore stack pointer
    popa
    ret
.diskError:
    mov $diskErrMsg, %si
    call BootPrint
    jmp NbloadPanic
#else
    pusha
    # Set BP to stack top at current moment
    # This is neccesary for us to read local variables
    mov %sp, %bp
    LOAD_BYTE $driveNumber %cl      # Get drive number
    xor %ch, %ch                    # Clear upper portion of DX
    push %cx                        # Save it
    # Save LBA low and high
    push %ax                    # Save sector low
    push %dx                    # Save sector high
    # Now we must convert it to CHS.
    # This is quite complex, to start, obtain the BIOS'es geometry
    mov -2(%bp), %dx     # Restore drive number
    mov $0x08, %ah      # BIOS get disk geometry
    push %di
    push %es
    mov $0, %bx         # Set ES and DI to zero to work around buggy BIOSes
    mov %bx, %es
    mov %bx, %di
    int $0x13
    jc .diskError
    pop %es
    pop %di
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
    mov $3, %cx
.biosRead:
    push %cx
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
    mov %di, %bx        # Offset goes in BX
    mov $2, %ah         # BIOS read sector
    mov $1, %al         # Sector count
    int $0x13           # Read it in
    jnc .done       # Did it work?
    pop %cx
    loop .biosRead
.done:
    mov %bp, %sp        # Restore stack pointer
    popa
    ret
.diskError:
    mov $diskErrMsg, %si
    call BootPrint
    jmp NbloadPanic
#endif

# Disk variables
driveNumber: .byte 0
#ifndef IS_FLOPPY
# DAP packet
biosDap:
    .byte 16
    .byte 0
    .byte 1
    .byte 0
    .word 0          # Offset to load into
    .word 0          # Segment to load into
    .quad 0         # Sector to read
#endif
diskErrMsg: .asciz "nbload: disk error"

welcomeBanner: .ascii "NexNix version "
.asciz NEXNIX_VERSION
