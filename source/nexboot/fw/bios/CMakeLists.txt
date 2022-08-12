#[[
    CMakeLists.txt - contains build system for BIOS builds
    Copyright 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
]]

cmake_minimum_required(VERSION 3.00)

# Ensure we are building for x86
if(NOT NEXNIX_BASEARCH STREQUAL "x86")
    message(FATAL_ERROR "BIOS nexboot only supported on x86 systems")
endif()

if(NOT NEXNIX_BOARD STREQUAL "pc")
    message(FATAL_ERROR "BIOS nexboot only supported on PC-based systems")
endif()

# Include boot record rules
include(boot/bootrec.cmake)

# Add boot records
add_boot_record(hdmbr boot/hdmbr.asm)
add_boot_record(hdvbr boot/hdvbr.asm)
