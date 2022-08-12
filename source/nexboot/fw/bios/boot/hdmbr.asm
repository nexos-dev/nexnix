; hdmbr.asm - contains MBR for hard disks
; Copyright 2022 The NexNix Project
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

;org 0
bits 16
cpu 8086
section .text

global start
start:
    mov ah, 0x0E
    mov al, 'N'
    int 10h
    cli
    hlt

times 440 - ($ - $$) db 0
