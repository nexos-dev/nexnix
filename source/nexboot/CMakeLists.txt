#[[
    CMakeLists.txt - contains build system for nexboot
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
project(nexboot C ASM)

if(NOT NEXBOOT_FW)
    message(FATAL_ERROR "firmware type not specified")
endif()

include(SystemBuild/ArchToMacro)
include(SystemBuild/Options)
include(GNUInstallDirs)

nexnix_add_parameter(NEXNIX_LOGLEVEL 
                     "Value that defines what should and shouldn't be logged"
                     2)
if(NEXNIX_ARCH STREQUAL "i386")
    nexnix_add_option(NEXNIX_I386_PAE "Specifies if PAE should be used" ON)
elseif(NEXNIX_ARCH STREQUAL "x86_64")
    nexnix_add_option(NEXNIX_X86_64_LA57 "Specifies if 5-level paging should be used" OFF)
endif()

# Add definition based off firmware type
if(${NEXBOOT_FW} STREQUAL "bios")
    add_compile_definitions(NEXNIX_FW_BIOS)
elseif(${NEXBOOT_FW} STREQUAL "efi")
    add_compile_definitions(NEXNIX_FW_EFI)
endif()

# Include main include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Install header files
# HACK ALERT: we install directly to /usr/include. There probably is a better way of doing this,
# but I don't know it
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include DESTINATION ${CMAKE_SYSROOT}/usr)

# Create object library of portable nexboot sources
list(APPEND NEXBOOT_SOURCES src/main.c)
add_library(nexboot OBJECT ${NEXBOOT_SOURCES})

add_subdirectory(fw/${NEXBOOT_FW})
