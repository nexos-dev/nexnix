#[[
    CMakeLists.txt - contains firmware specific build process
    Copyright 2022, 2023 The NexNix Project

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

cmake_minimum_required(VERSION 3.7)

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
add_boot_record(hdmbr ${CMAKE_CURRENT_SOURCE_DIR}/boot/hdmbr.asm
                      ${CMAKE_CURRENT_BINARY_DIR}/hdmbr "-DIS_NOFLOPPY")
add_boot_record(gptmbr ${CMAKE_CURRENT_SOURCE_DIR}/boot/gptmbr.asm 
                       ${CMAKE_CURRENT_BINARY_DIR}/gptmbr "-DIS_NOFLOPPY")
add_boot_record(fdmbr ${CMAKE_CURRENT_SOURCE_DIR}/boot/fdmbr.asm
                      ${CMAKE_CURRENT_BINARY_DIR}/fdmbr
                      "")
add_boot_record(hdvbr ${CMAKE_CURRENT_SOURCE_DIR}/boot/hdvbr.asm 
                      ${CMAKE_CURRENT_BINARY_DIR}/hdvbr "-DIS_NOFLOPPY")
add_boot_record(isombr ${CMAKE_CURRENT_SOURCE_DIR}/boot/isombr.asm 
                      ${CMAKE_CURRENT_BINARY_DIR}/isombr "-DIS_NOFLOPPY")

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
get_target_property(NBLOAD_OUTPUT nbload OUTPUT)
message(${NBLOAD_OUTPUT})
add_custom_command(OUTPUT ${NEXBOOT_OUTPUT}
                   COMMAND cat ${NBLOAD_OUTPUT} $<TARGET_FILE:nexboot-bios-1> >
                   ${NEXBOOT_OUTPUT} DEPENDS ${NBLOAD_OUTPUT} nexboot-bios-1)
add_custom_target(nexboot-bios ALL DEPENDS ${NEXBOOT_OUTPUT})
# Install to root of boot partition
install(FILES ${NEXBOOT_OUTPUT} DESTINATION ..)
