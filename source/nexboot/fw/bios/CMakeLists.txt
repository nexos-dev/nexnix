#[[
    CMakeLists.txt - contains firmware specific build process
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

cmake_minimum_required(VERSION 3.12)

# Build firmware-specific targets

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
add_boot_record(hdmbr boot/hdmbr.S TRUE)
add_boot_record(gptmbr boot/gptmbr.S TRUE)
add_boot_record(fdmbr boot/fdmbr.S FALSE)
add_boot_record(hdvbr boot/hdvbr.S FALSE)
add_boot_record(isombr boot/isombr.S FALSE)

# Let floppy MBR know that it's the floppy MBR
target_compile_definitions(fdmbr PRIVATE IS_FLOPPY)

# Add nbload
add_subdirectory(nbload)

# Add bootloader
add_executable(nexboot-bios-1 bios.c)
# Set linker script
target_link_options(nexboot-bios-1 PRIVATE -T 
                                   ${CMAKE_CURRENT_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/link.ld)
target_link_libraries(nexboot-bios-1 PRIVATE nexboot)

# Add a custom command to concatenate nexboot-bios and nbload
set(NEXBOOT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/nexboot")
add_custom_command(OUTPUT ${NEXBOOT_OUTPUT}
                   COMMAND cat $<TARGET_FILE:nbload> $<TARGET_FILE:nexboot-bios-1> >
                   ${NEXBOOT_OUTPUT} DEPENDS nbload nexboot-bios-1)
add_custom_target(nexboot-bios ALL DEPENDS ${NEXBOOT_OUTPUT})
# Install to root of boot partition
install(FILES ${NEXBOOT_OUTPUT} DESTINATION ..)
