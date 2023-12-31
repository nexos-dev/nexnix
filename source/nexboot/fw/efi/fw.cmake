#[[
    fw.cmake - contains firmware specific build process
    Copyright 2023 The NexNix Project

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

set(NEXBOOT_CFLAGS $<$<COMPILE_LANGUAGE:C>:-fshort-wchar> $<$<COMPILE_LANGUAGE:C>:-fPIE> $<$<COMPILE_LANGUAGE:ASM>:-fPIE>)

if("${NEXNIX_ARCH}" STREQUAL "x86_64")
    set(NEXBOOT_CFLAGS ${NEXBOOT_CFLAGS} $<$<COMPILE_LANGUAGE:C>:-maccumulate-outgoing-args> -DEFI_FUNCTION_WRAPPER -DHAVE_USE_MS_ABI -DGNU_EFI_USE_MS_ABI)
elseif("${NEXNIX_ARCH}" STREQUAL "riscv64")
    set(NEXBOOT_CFLAGS ${NEXBOOT_CFLAGS} -mno-relax)
elseif("${NEXNIX_ARCH}" STREQUAL "armv8")
    set(NEXBOOT_CFLAGS ${NEXBOOT_CFLAGS} -fno-jump-tables)
endif()

# Grab GNU-EFI package
find_package(PkgConfig REQUIRED)
pkg_check_modules(GNUEFI REQUIRED IMPORTED_TARGET gnu-efi)
set(NEXBOOT_CFLAGS ${NEXBOOT_CFLAGS} ${GNUEFI_CFLAGS})

# Headers
list(APPEND NEXBOOT_FW_HEADERS include/nexboot/efi/efi.h)
