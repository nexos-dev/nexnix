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
bpbRootEntCount: .word 0            # Unused
bpbTotSector16: .word 0             # Unused
bpbMedia: .byte 0                   # Unused
bpbFatSize16: .word 0               # Unused
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
    jmp NbloadMain

#include "nbload.inc"
#include "nbload2.inc"

NbloadMain:
    cli
    hlt

.fill 510 - (. - mbrBase), 1, 0
.word 0xAA55
