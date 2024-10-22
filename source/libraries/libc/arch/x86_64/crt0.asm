; crt0.asm - contains program entry point
; Copyright 2024 The NexNix Project
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

global _start
extern __libc_main
extern _exit

_start:
    xor rbp, rbp            ; Set first frame pointer so backtraces know when to stop
    call __libc_main        ; Start C program
    ; EAX contains return value of program
    push rax
    call _exit
    ; Shouldn't get here
    ud2
