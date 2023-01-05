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

cmake_minimum_required(VERSION 3.12)

# Create bootlader target
add_executable(nexboot-efi-1 efimain.c)

# Add all EFI compiler options
target_compile_options(nexboot-efi-1 PRIVATE -fno-pic -fno-plt -fshort-wchar)
target_include_directories(nexboot-efi-1 PRIVATE nexboot)
target_compile_definitions(nexboot-efi-1 PRIVATE HAVE_USE_MS_ABI)
# Add linker options
target_link_options(nexboot-efi-1 PRIVATE -Wl,-q -Wl,-S -T
                                        ${CMAKE_CURRENT_SOURCE_DIR}/cpu/${NEXNIX_ARCH}/link.ld
                                        -znocombreloc)
# Link to standard nexboot stuff
target_link_libraries(nexboot-efi-1 PRIVATE nexboot)
# Use nnelf2efi to convert to EFI image
# Figure out which nnelf2efi type to use
if(${NEXNIX_ARCH} STREQUAL "i386")
    set(NNELF2EFI_BITS 32)
elseif(${NEXNIX_ARCH} STREQUAL "x86_64")
    set(NNELF2EFI_BITS 64)
endif()
set(NEXBOOT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/nexboot.efi")
add_custom_command(OUTPUT ${NEXBOOT_OUTPUT}
                   COMMAND nnelf2efi${NNELF2EFI_BITS} 
                   -o ${NEXBOOT_OUTPUT} $<TARGET_FILE:nexboot-efi-1>
                   DEPENDS nexboot-efi-1)
add_custom_target(nexboot-efi ALL DEPENDS ${NEXBOOT_OUTPUT})
install(FILES ${NEXBOOT_OUTPUT} DESTINATION .)
