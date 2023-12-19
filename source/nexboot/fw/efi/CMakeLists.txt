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

# For right now, only GNU toolchain support EFI
if(NOT "${NEXNIX_TOOLCHAIN}" STREQUAL "gnu")
    message(FATAL_ERROR "GNU toolchain required for nexboot-efi")
endif()

# Source files
list(APPEND NEXBOOT_SOURCE_FILES efimain.c
    efimem.c
    efi.c
    hwdetect.c
    ${CMAKE_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/eficall.S)
add_executable(nexboot-efi ${NEXBOOT_SOURCE_FILES})

# Libraries
target_link_libraries(nexboot-efi PRIVATE nexboot)

# Options
target_link_options(nexboot-efi PUBLIC -Wl,-s -Wl,-q -Wl,-Bsymbolic -Wl,-Bsymbolic-functions
    -T ${CMAKE_CURRENT_SOURCE_DIR}/link/link-${NEXNIX_ARCH}.ld)

# Convert to EFI using elf2efi
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi
    COMMAND $ENV{NNBUILDROOT}/tools/bin/elf2efi${NEXNIX_BITS}
    -s 10 $<TARGET_FILE:nexboot-efi>
    ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi
    DEPENDS nexboot-efi)
add_custom_target(nexboot-efi-1 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi DESTINATION .)
