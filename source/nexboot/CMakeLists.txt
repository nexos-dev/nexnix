#[[
    CMakeLists.txt - contains build system for nexboot
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
project(nexboot C ASM)

if(NOT NEXBOOT_FW)
    message(FATAL_ERROR "firmware type not specified")
endif()

if(NOT NEXBOOT_GRAPHICS_MODE)
    set(NEXBOOT_GRAPHICS_MODE "text" CACHE STRING "Graphics output mode for nexboot")
endif()

if(NOT NEXBOOT_LOGLEVEL)
    set(NEXBOOT_LOGLEVEL "4" CACHE STRING "Log output level for messages")
endif()

# NOTE! We exclude NEXNIX_FW here because it may be changed by firmware-specific
# processing
add_compile_definitions(NEXBOOT_LOGLEVEL=${NEXBOOT_LOGLEVEL} 
                        NEXBOOT_GRAPHICS_MODE=${NEXBOOT_GRAPHICS_MODE}
                        NEXBOOT_ARCH=${NEXNIX_ARCH}
                        NEXBOOT_BOARD=${NEXNIX_BOARD})

# Create object library of portable nexboot sources
list(APPEND NEXBOOT_SOURCES src/main.c)
add_library(nexboot OBJECT ${NEXBOOT_SOURCES})

add_subdirectory(fw/${NEXBOOT_FW})
