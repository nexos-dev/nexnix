#[[
    CMakeLists.txt - contains build system for nexke
    Copyright 2023 - 2024 The NexNix Project

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
project(nexke C ASM ASM_NASM)

include(SystemBuild/ArchToMacro)
include(SystemBuild/Options)
include(GNUInstallDirs)

# Check for options
if(NOT NEXNIX_ARCH)
    set(NEXNIX_ARCH i386)
endif()

if(NOT NEXNIX_BOARD)
    set(NEXNIX_BOARD pc)
endif()

if(NOT NEXNIX_TOOLCHAIN)
    message(FATAL_ERROR "toolchain not specified")
endif()

if(NOT NEXNIX_TARGETCONF)
    if(NEXNIX_BOARD STREQUAL "pc")
        set(NEXNIX_TARGETCONF "acpi")
    elseif(NEXNIX_BOARD STREQUAL "sbsa")
        set(NEXNIX_TARGETCONF "sbsa")
    elseif(NEXNIX_BOARD STREQUAL "generic")
        set(NEXNIX_TARGETCONF "rvefi")
    endif()
endif()

if(NOT NEXNIX_BASEARCH)
    if(NEXNIX_ARCH STREQUAL "i386")
        set(NEXNIX_BASEARCH "x86")
    endif()
endif()

# Include modules
include(SystemBuild/ArchToMacro)
include(SystemBuild/Options)
include(GNUInstallDirs)

nexnix_add_parameter(NEXNIX_LOGLEVEL
    "Value that defines what should and shouldn't be logged"
    2)

if(NEXKE_UP STREQUAL "1")
    add_definitions(-DNEXKE_UP)
endif()

# Include includes directory
include_directories(include)

# Include CPU build stuff
include(cpu/${NEXNIX_ARCH}/arch.cmake)

# Include platform build stuff
include(platform/${NEXNIX_BOARD}/platform.cmake)

# Install header files
# HACK ALERT: we install directly to /usr/include. There probably is a better way of doing this,
# but I don't know it
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include DESTINATION ${CMAKE_SYSROOT}/usr)

# nexke source files
list(APPEND NEXKE_SOURCES
    core/main.c
    core/log.c
    core/fbcons.c
    core/time.c
    core/resource.c
    core/work.c
    mm/slab.c
    mm/space.c
    mm/malloc.c
    mm/page.c
    mm/object.c
    mm/kvmm.c
    mm/fault.c
    platform/interrupt.c
    platform/acpi.c
    task/thread.c
    task/sched.c
    task/wait.c
    task/synch.c)

# Create kernel
add_executable(nexke ${NEXKE_SOURCES})

# Add compiler flags
add_compile_definitions(NEXKE_ARCH_HEADER=\"${NEXKE_ARCH_HEADER}\")

# Add link options
target_compile_options(nexke PRIVATE)
target_link_options(nexke PRIVATE -Wl,-no-pie -T ${NEXKE_LINKER_SCRIPT})

# Find libk
find_library(NEXKE_LK_LOCATION k REQUIRED)
get_filename_component(NEXKE_LK_LOCATION ${NEXKE_LK_LOCATION} REALPATH)
add_custom_target(libk DEPENDS ${NEXKE_LK_LOCATION})

target_link_libraries(nexke PUBLIC ${NEXKE_LK_LOCATION})
add_dependencies(nexke libk)

install(TARGETS nexke DESTINATION .)
