# common.inc - contains MBR/VBR common code
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

# Macros to help with variable references
.macro LOAD_BYTE addr, reg
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movb (%bx), \reg
.endm

.macro LOAD_WORD addr, reg
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movw (%bx), \reg
.endm

.macro LOAD_DWORD addr, reg
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movl (%bx), \reg
.endm

.macro STORE_BYTE reg, addr
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movb \reg, (%bx)
.endm

.macro STORE_WORD reg, addr
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movw \reg, (%bx)
.endm

.macro STORE_DWORD reg, addr
    mov \addr, %bx
    add $LABEL_OFFSET, %bx
    movl \reg, (%bx)
.endm

# Prints a null-terminated string to the screen
# Input: SI = string pointer. LABEL_OFFSET gets added automatically
BootPrint:
    push %ax
    add $LABEL_OFFSET, %si
    # Prepare function code now
    mov $0x0E, %ah
.printLoop:
    lodsb
    # Check for completion
    cmp $0, %al
    je .printDone
    # Print it out
    int $0x10
    jmp .printLoop
.printDone:
    pop %ax
    ret
