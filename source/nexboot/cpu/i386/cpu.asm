; cpu.asm - contains CPU asm code
; Copyright 2023 - 2024 The NexNix Project
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

global nbCpuAsmLaunch

nbCpuAsmLaunch:
    cli
    mov ebp, esp
    ; Get arguments
    mov eax, [ebp+4]
    mov ebx, [ebp+8]
    mov ecx, [ebp+12]
    ; Set stack
    mov esp, eax
    mov ebp, 0          ; Set a new zero frame
    ; Pass bootinfo as parameter
    push ecx
    call ebx
    ; If we get here, halt
    cli
    hlt
