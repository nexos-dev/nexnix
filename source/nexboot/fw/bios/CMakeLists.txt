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
add_boot_record(hdmbr1 ${CMAKE_CURRENT_SOURCE_DIR}/boot/hdmbr.asm
                      ${CMAKE_CURRENT_BINARY_DIR}/hdmbr "-DIS_NOFLOPPY")
add_boot_record(gptmbr1 ${CMAKE_CURRENT_SOURCE_DIR}/boot/gptmbr.asm 
                       ${CMAKE_CURRENT_BINARY_DIR}/gptmbr "-DIS_NOFLOPPY")
add_boot_record(fdmbr1 ${CMAKE_CURRENT_SOURCE_DIR}/boot/fdmbr.asm
                      ${CMAKE_CURRENT_BINARY_DIR}/fdmbr
                      "")
add_boot_record(hdvbr1 ${CMAKE_CURRENT_SOURCE_DIR}/boot/hdvbr.asm 
                      ${CMAKE_CURRENT_BINARY_DIR}/hdvbr "-DIS_NOFLOPPY")
add_boot_record(isombr1 ${CMAKE_CURRENT_SOURCE_DIR}/boot/isombr.asm 
                      ${CMAKE_CURRENT_BINARY_DIR}/isombr "-DIS_NOFLOPPY")

# Add nbload
get_property(defines GLOBAL PROPERTY NEXNIX_ARCH_MACROS)
add_boot_record(nbload1 ${CMAKE_CURRENT_SOURCE_DIR}/nbload/nbload.asm
                       ${CMAKE_CURRENT_BINARY_DIR}/nbload "${defines}")

# Build bioscall
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bioscall.bin
                   COMMAND nasm -fbin 
                           ${CMAKE_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/bioscall.asm
                           -o ${CMAKE_CURRENT_BINARY_DIR}/bioscall.bin
                    DEPENDS ${CMAKE_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/bioscall.asm)
set(NEXBOOT_BIOSCALL_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/bioscall.bin)

# Generate source file wrapper
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bioscall.asm.in ${CMAKE_CURRENT_BINARY_DIR}/bioscall.asm)
# Make it dependent on bioscall
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/bioscall.asm PROPERTIES OBJECT_DEPENDS
                            ${NEXBOOT_BIOSCALL_LOCATION})
# Add bootloader
list(APPEND NEXBOOT_SOURCE_FILES 
            bios.c
            biosmem.c
            hwdetect.c
            chainload.c
            ${CMAKE_CURRENT_BINARY_DIR}/bioscall.asm)
add_executable(nexboot-bios-1 ${NEXBOOT_SOURCE_FILES})
# Set linker script
target_link_options(nexboot-bios-1 PRIVATE -Wl,-no-pie -T 
                                   ${CMAKE_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/link.ld)
target_link_libraries(nexboot-bios-1 PRIVATE nexboot)

# Compress nexboot
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/znexboot 
                   COMMAND gzip -c $<TARGET_FILE:nexboot-bios-1>
                           > ${CMAKE_CURRENT_BINARY_DIR}/znexboot
                   DEPENDS nexboot-bios-1)
add_custom_target(znexboot2 DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/znexboot)

# Build decompressor
add_executable(ndecomp decomp/decomp.c decomp/tinf/tinflate.c 
                       decomp/tinf/tinfgzip.c decomp/tinf/tinfzlib.c 
                       decomp/tinf/adler32.c decomp/tinf/crc32.c)
# Set linker script
target_link_options(ndecomp PRIVATE -Wl,-no-pie -T 
                            ${CMAKE_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/decomp_link.ld)
target_link_libraries(ndecomp PRIVATE ${NEXBOOT_LK_LOCATION})

# Add a custom command to concatenate nexboot-bios and nbload
set(NEXBOOT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/nexboot")
get_target_property(NBLOAD_OUTPUT nbload1 OUTPUT)
add_custom_command(OUTPUT ${NEXBOOT_OUTPUT}
                   COMMAND cat ${NBLOAD_OUTPUT} $<TARGET_FILE:ndecomp>
                   ${CMAKE_CURRENT_BINARY_DIR}/znexboot >
                   ${NEXBOOT_OUTPUT} DEPENDS ${NBLOAD_OUTPUT} ndecomp znexboot2 nexboot-bios-1)
add_custom_target(nexboot-bios ALL DEPENDS ${NEXBOOT_OUTPUT})
# Install to root of boot partition
install(FILES ${NEXBOOT_OUTPUT} DESTINATION .)
