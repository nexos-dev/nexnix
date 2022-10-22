# nbload.S - contains nbload logic
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

# Start of bootloader detection data
#define NBLOAD_DETECT_RESULT 0x700
# Top of stack
#define NBLOAD_STACK_TOP 0xFFFF
# Log-related bits
#define NBLOAD_LOG_START 0x600
#define NBLOAD_LOG_SIZE 0x100

#define NBLOAD_PMODE_ENTRY 0x11000
#define NBLOAD_BASE 0x10000
#define NBLOAD_BASE_SEG 0x1000

#include <detect.h>

.global NbStartDetect
NbStartDetect:
    # Set state
    mov $NBLOAD_BASE_SEG, %ax
    mov %ax, %ds
    mov $0x0, %ax
    mov %ax, %ss
    mov %ax, %es
    mov $NBLOAD_STACK_TOP, %sp
    # Save drive number
    push %dx
    mov %sp, %bp
    # Initialize detection structure
    mov $NBLOAD_DETECT_RESULT, %di
    movl $NBLOAD_SIGNATURE, (%di)               # Set signature
    movw $NBLOAD_LOG_START, 4(%di)              # Set log start offset
    movw $0, 6(%di)                             # Set log start segment
    movw $NBLOAD_LOG_SIZE, 8(%di)               # Set log size
    # CPU detection
    movb $NBLOAD_CPU_FAMILY_X86, 12(%di)        # Set CPU family
    movb $NBLOAD_CPU_ARCH_I386, 13(%di)         # Set CPU architecture
    # Detect wheter we are on a CPU with CPUID, or if we are on a 486, or a 386
    # Intel says that we must check if EFLAGS.ID is modifiable to determine
    pushfl                          # Get EFLAGS
    pop %eax
    xor $(1 << 21), %eax            # Change EFLAGS.ID
    mov %eax, %ecx                  # Store for reference
    push %eax                       # Store new EFLAGS
    popfl
    pushfl                          # Get new EFLAGS
    pop %eax
    cmp %ecx, %eax                  # Check if they're different
    je .isCpuid                    # Nope, store that
    # We are on either or 486 or a 386.
    # How do we decide? Based on wheter EFLAGS.AC is modifiable
    pushfl                          # Get EFLAGS
    pop %eax
    xor $(1 << 18), %eax            # Change EFLAGS.AC
    mov %eax, %ecx                  # Store for reference
    push %eax                       # Store new EFLAGS
    popfl
    pushfl                          # Get new EFLAGS
    pop %eax
    cmp %ecx, %eax
    je .is486                      # Nope, store that
.is386:
    movw $NBLOAD_CPU_VERSION_386, 14(%di)   # Set it as 386
    # Print message
    mov $cpu386Message, %si
    mov $4, %cx
    call NbloadLogMsg
    jmp .cpuCheckDone
.is486:
    movw $NBLOAD_CPU_VERSION_486, 14(%di)   # Set it as 486
    # Print message
    mov $cpu486Message, %si
    mov $4, %cx
    call NbloadLogMsg
    # Set CR0.WP
    mov %cr0, %eax
    or $(1 << 16), %eax
    mov %eax, %cr0
    jmp .cpuCheckDone
.isCpuid:
    movw $NBLOAD_CPU_VERSION_CPUID, 14(%di) # Let other layers know to use CPUID
#ifdef NEXNIX_I386_PAE
    # Check for PAE now since we know we have CPUID
    mov $1, %eax
    cpuid
    # Check for PAE flag in EDX
    test $(1 << 6), %edx            # Test PAE flag
    jz .noPae                       # If no PAE, panic
#endif
    # Print message
    mov $cpuCpuidMessage, %si
    mov $4, %cx
    call NbloadLogMsg
    # Set CR0.WP
    mov %cr0, %eax
    or $(1 << 16), %eax
    mov %eax, %cr0
    jmp .cpuCheckDone
.noPae:
    # Print message and panic
    mov $noPaeMessage, %si
    mov $1, %cx
    call NbloadLogMsg
    call NbloadPanic
.cpuCheckDone:
    # FPU check
    # Check if an x87 coprocessor exists, and set CR0 accordingly
    fninit                          # Initialize it
    fstsw %ax                       # Get status word
    cmp $0, %al                     # Check if its 0
    je .fpuExists                   # If equal, FPU exists
    mov %cr0, %eax
    or $(1 << 2), %eax              # Set CR0.EM
    mov %eax, %cr0
    movb $0, 16(%di)                # Inform nexboot of this
    # As of now, software-emulated FPU isn't supported
    # Error out if an FPU doesn't exist
    mov $noFpuMessage, %si
    mov $1, %cx
    call NbloadLogMsg
    call NbloadPanic
    jmp .fpuCheckDone
.fpuExists:
    movb $(1 << 0), 16(%di)         # Inform nexboot of this
    # Print message
    mov $fpuMessage, %si
    mov $4, %cx
    call NbloadLogMsg
.fpuCheckDone:
    # Now we must enable the A20 gate
    call NbloadWaitInputBuf     # Wait for input buffer to be empty
    mov $0xAD, %al              # Command 0xAD disables keyboard
    out %al, $0x64              # Disable it
    call NbloadWaitInputBuf     # Wait for input buffer to be empty
    mov $0xD0, %al              # Command 0xD0 reads configuration byte
    out %al, $0x64              # Grab configuration byte
    call NbloadWaitOutputBuf    # Wait for output buffer to be full
    in $0x60, %al               # Read in configuration byte
    mov %al, %dl
    or $(1 << 1), %dl           # Set A20 bit
    call NbloadWaitInputBuf     # Wait for input buffer
    mov $0xD1, %al              # Command 0xD1 writes configuration byte
    out %al, $0x64              # Set command to write conf byte
    call NbloadWaitInputBuf     # Wait for input buffer
    mov %dl, %al
    out %al, $0x60              # Write new byte
    call NbloadWaitInputBuf
    mov $0xAE, %al              # Command 0xAE enables keyboard
    out %al, $0x64              # Enable it
    call NbloadWaitInputBuf
    push %es
    mov $0xFFFF, %ax            # Load 0xFFFF as segment
    mov %ax, %es
    movw $0xAA88, %es:0x7E0E     # Load value to high memory
    pop %es
    cmpw $0xAA88, 0x7DFE
    je .a20failed               # If equal, A20 check failed
    # Now we must enter protected mode...
    cli                             # Disable interrupts
    lgdt gdtPtr                     # Load the GDT
    mov %cr0, %eax                  # Grab CR0
    or $(1 << 0), %eax              # Set the PE bit
    mov %eax, %cr0                  # Update CR0 with PE bit set
    ljmpl $0x18, $NBLOAD_PMODE_ENTRY # Far return to protected mode
.a20failed:
    mov $a20failMessage, %si
    mov $1, %cx
    call NbloadLogMsg           # Print out error
    call NbloadPanic

#define LABEL_OFFSET 0x0
#include <common.inc>

# Waits for keyboard input buffer
NbloadWaitInputBuf:
.wait:
    in $0x64, %al               # Read flags
    test $(1 << 1), %al         # Check input buffer empty
    jnz .wait
    ret

# Waits for keyboard output buffer
NbloadWaitOutputBuf:
.wait2:
    in $0x64, %al               # Read flags
    test $(1 << 0), %al         # Check output buffer full
    jz .wait2
    ret

# Waits for key strike and goes to next boot device
NbloadPanic:
    mov $0, %ah
    int $0x16
    int $0x18

# Logs a message to boot log
# Input: DS:SI = string to write, CX = log level of message
NbloadLogMsg:
    pusha
    # Write message out to log
    mov %si, %bx                # Save SI
    movw $logLocation, %si
    mov (%si), %si              # Put log location in SI
    push %ds
    mov $0, %dx                 # So we can access log
    mov %dx, %ds
    # Format of log entry:
    # Offset 0 - 1: Message offset
    # Offset 2 - 3: Message segment
    # Offset 4 - 5: Log level
    mov %bx, (%si)              # Write message offset
    mov %ds, 2(%si)             # Write segment
    mov %cx, 4(%si)             # Write log level
    pop %ds
    # Write out new log location
    add $6, %si                 # Move to next log entry
    movw $logLocation, %di
    mov %si, (%di)
    mov %bx, %si                # Get message back in SI
    # Decide where to print
    cmp $NEXNIX_LOGLEVEL, %cx
    jg .logDone                    # Print to serial port if message level
                                # is greater than system level
    call BootPrint              # Print to screen if allowed, then to serial port
.logDone:
    popa
    ret

# The current log pointer
logLocation: .word NBLOAD_LOG_START

cpuCpuidMessage: .asciz "\r\nnbload: detected CPU 486+"
cpu486Message: .asciz "\r\nnbload: detected CPU 486"
cpu386Message: .asciz "\r\nnbload: detected CPU 386"
fpuMessage: .asciz "\r\nnbload: x87 FPU found"
noFpuMessage: .asciz "\r\nnbload: no x87 FPU found"
a20failMessage: .asciz "\r\nnbload: unable to enable A20 gate"
noPaeMessage: .asciz "\r\nnbload: PAE not supported on CPU"

# Protected mode data structures

# The global descriptor table
gdtBase:
    # Null descriptor
    .word 0
    .word 0
    .byte 0
    .byte 0
    .byte 0
    .byte 0
    # 16 bit code segment descriptor
    .word 0xFFFF                # Low 16 bits of limit
    .word 0                     # Low 16 bits of base
    .byte 0x2                   # High 8 bits of base
    .byte 0x98                  # Access byte. Execute-only code segment
                                # that is non-conforming and present
    .byte 0                     # Reserved
    .byte 0                     # Reserved
    # 16 bit data segment descriptor
    .word 0xFFFF                # Low 16 bits of limit
    .word 0                     # Low 16 bits of base
    .byte 0x2                   # High 8 bits of base
    .byte 0x92                  # Read-write data segment
    .byte 0                     # Reserved
    .byte 0                     # Reserved
    # 32 bit code segment descriptor
    .word 0xFFFF                # Low 16 bits of limit
    .word 0                     # Low 16 bits of base
    .byte 0                     # Middle 8 bits of base
    .byte 0x98                  # Access byte. Execute-only code segment
                                # that is non-conforming and present
    .byte 0xCF                  # Top 4 bits of limit = 0xF, granularity = 4K,
                                # D bit = 1, indicating a 32 bit code segment
    .byte 0                     # Top 8 bits of base
    # 32 bit data segment descriptor
    .word 0xFFFF                # Low 16 bits of limit
    .word 0                     # Low 16 bits of base
    .byte 0                     # Middle 8 bits of base
    .byte 0x92                  # Read write data segment
    .byte 0xCF                  # Top 4 bits of limit = 0xF, granularity = 4K,
                                # B bit = 1, indicating 32 bit stack when in SS
    .byte 0                     # Top 8 bits of base
gdtEnd:

# GDTR structure
gdtPtr:
    .word gdtEnd - gdtBase - 1          # Limit = last addressable byte in table
    .long gdtBase + 0x10000             # Start of GDT

.align 4096
.code32

# Memory locations
#define NBLOAD_PMODE_STACK 0x7FFFC
#define NBLOAD_NEXBOOT_BASE 0x50000

# Paging structure locations
#ifdef NEXNIX_ARCH_I386
#ifndef NEXNIX_I386_PAE
#define NBLOAD_PDIR_BASE 0x3000
#define NBLOAD_PTAB_BASE 0x4000
#else
#define NBLOAD_PDIR_BASE 0x3000
#define NBLOAD_PDPT_BASE 0x4000
#endif
#endif

# Protected mode entry point
NbloadStartPmode:
    # Set data segments
    mov $0x20, %ax              # 0x20 = data segment
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    # Set up a stack
    mov $NBLOAD_PMODE_STACK, %esp
    # Set up paging structures
#ifdef NEXNIX_I386_PAE
    mov $NBLOAD_PDPT_BASE, %esi     # Get PDPT address
    mov %esi, %edi                  # Prepare to zero it
    mov $0, %al
    mov $24, %ecx                   # 24 bytes to 0
    rep stosb
    # Map directory in PDPT
    mov $NBLOAD_PDIR_BASE, %ebx     # Get address of page directory
    mov %ebx, %edi                  # Prepare to zero it
    mov $0, %al
    mov $0x1000, %ecx               # Zero out a page
    rep stosb
    or $1, %ebx                     # Set as present and writable
    mov %ebx, (%esi)                # Map page directory. Note that we only work with
                                    # the low 32 bits and leave the top bits zeroed
    # Create PDE
    mov $NBLOAD_PDIR_BASE, %esi     # Get page directory address
    mov $0x83, %eax                 # Prepare entry. NOTE: we only use low 4 bytes
                                    # of entry, leaving upper 4 zeroed
                                    # Entry is present, writable, and large
                                    # Address mapped is address 0
    mov %eax, (%esi)                # Map it
    xchg %bx, %bx
    # Enable paging now
    mov %cr4, %eax                  # Get CR4 to enable PAE
    or $(1 << 5), %eax              # Set PAE bit
    mov %eax, %cr4                  # Put in CR4
    mov $NBLOAD_PDPT_BASE, %edx
    mov %edx, %cr3                  # Load CR3
    mov %cr0, %eax                  # Get CR0
    or $(1 << 31), %eax             # Set PG bit
    mov %eax, %cr0                  # Enable paging
    # Set EFER.NX
    mov $0xC0000080, %ecx
    rdmsr                           # Read the MSR
    or $(1 << 11), %eax             # Set bit 11 (NX bit)
    wrmsr
#else
    mov $NBLOAD_PDIR_BASE, %esi     # Get page directory address
    mov %esi, %edi                  # Prepare to zero it
    mov $0, %al
    mov $0x1000, %ecx               # 4096 bytes to 0
    rep stosb
    # Create PDE for mapping
    mov $NBLOAD_PTAB_BASE, %ebx     # Get address of page table
    mov %ebx, %edi                  # Prepare to zero it
    mov $0, %al
    mov $0x1000, %ecx
    rep stosb
    or $3, %ebx                     # Set present and writable bits
    mov %ebx, (%esi)                # Put in page directory
    # Fill page table
    mov $NBLOAD_PTAB_BASE, %edi     # Get page table address
    mov $1024, %ecx                 # 1024 PTEs in a page table
    mov $0, %edx                    # Start at address 0
.ptLoop:
    mov %edx, %ebx                  # Get address in EBX
    or $3, %ebx                     # Set present and writable bits
    mov %ebx, (%edi)                # Store in page table
    add $0x1000, %edx               # Move to next page
    add $4, %edi                    # Move to next PTE
    loop .ptLoop
    mov %esi, %cr3                  # Load page directory
    mov %cr0, %eax                  # Get CR0
    or $(1 << 31), %eax             # Set PG bit
    mov %eax, %cr0                  # Enable paging
#endif
    # Now we must load up the ELF bootloader
    # Get base of it
    mov $end, %esi                  # Get end of this part
    add $NBLOAD_BASE, %esi
    mov 28(%esi), %ebx              # Get program header offset
    add %esi, %ebx                  # Add base of ELF
    push %esi                       # Save base
    mov 4(%ebx), %edx               # Get program header data offset from phdr
    add %esi, %edx
    mov %edx, %esi                  # So we can use it rep movsb
    mov 16(%ebx), %ecx              # Get section file size
    mov $NBLOAD_NEXBOOT_BASE, %edi  # Get nexboot base address
    rep movsb                       # Copy out
    # Get difference between memory and file size, and zero that out
    mov 20(%ebx), %edx              # Get memory size
    mov 16(%ebx), %ecx              # Get file size
    sub %ecx, %edx                  # Subtract
    mov $NBLOAD_NEXBOOT_BASE, %edi  # Get nexboot base in EDI
    add %ecx, %edi                  # Move to area to be zeroed
    mov %edx, %ecx                  # Set loop counter to memory-file difference
    mov $0, %al                     # Store a zero
    rep stosb                       # Store it
    # Get entry point
    pop %esi
    mov 24(%esi), %ebx
    jmp *%ebx                       # Jump to it
end:
