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

# Grab GNU-EFI package
find_package(PkgConfig REQUIRED)
pkg_check_modules(GNUEFI REQUIRED IMPORTED_TARGET gnu-efi)

# Get GNU-EFI architecture
if(${NEXNIX_ARCH} STREQUAL "i386")
    set(GNUEFI_ARCH "ia32")
elseif(${NEXNIX_ARCH} STREQUAL "armv8")
    set(GNUEFI_ARCH "aarch64")
else()
    set(GNUEFI_ARCH ${NEXNIX_ARCH})
endif()

# Source files
list(APPEND NEXBOOT_SOURCE_FILES efimain.c
    efimem.c
    efi.c
    hwdetect.c)
add_executable(nexboot-efi-1 EXCLUDE_FROM_ALL ${NEXBOOT_SOURCE_FILES})

# Includes
target_include_directories(nexboot-efi-1 PUBLIC ${GNUEFI_CFLAGS})

# Libraries
target_link_libraries(nexboot-efi-1 PRIVATE nexboot gnuefi)

# Options
target_link_options(nexboot-efi-1 PUBLIC -shared -Wl,-Bsymbolic
    -T ${CMAKE_SYSROOT}/${GNUEFI_LIBDIR}/elf_${GNUEFI_ARCH}_efi.lds
    -L ${GNUEFI_LIBDIR} -znocombreloc
    ${CMAKE_SYSROOT}/${GNUEFI_LIBDIR}/crt0-efi-${GNUEFI_ARCH}.o)

target_compile_options(nexboot-efi-1 PUBLIC ${NEXBOOT_CFLAGS})

# Convert to EFI file with objcopy
if(${NEXNIX_ARCH} STREQUAL "armv8")
    set(OBJCOPY_FMT "efi-app-aarch64")
elseif(${NEXNIX_ARCH} STREQUAL "riscv64")
    set(OBJCOPY_FMT "binary")
else()
    set(OBJCOPY_FMT efi-app-${NEXNIX_ARCH})
endif()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi
    COMMAND ${CMAKE_OBJCOPY} -j .text -j .sdata -j .data
    -j .dynamic -j .rodata -j .rel
    -j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* -j .reloc -j .areloc
    -O ${OBJCOPY_FMT} $<TARGET_FILE:nexboot-efi-1>
    ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi
    DEPENDS nexboot-efi-1)
add_custom_target(nexboot-efi ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi DESTINATION .)
