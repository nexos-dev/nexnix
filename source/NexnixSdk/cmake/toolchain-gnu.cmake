#[[
    gnu_toolchain.cmake - contains GNU cross compiliation stuff
    Copyright 2021, 2022 The NexNix Project

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

set(NEXNIX_EXETYPE elf)
set(NEXNIX_TOOLCHAINPREFIX $ENV{NNTOOLCHAINPATH}/${NEXNIX_TARGET}-${NEXNIX_EXETYPE})

set(CMAKE_SYSTEM_NAME NexNix)
set(CMAKE_SYSTEM_VERSION 0.0.1)

set(CMAKE_C_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-g++)
set(CMAKE_AR ${NEXNIX_TOOLCHAINPREFIX}-ar)
if("$ENV{NNCOMMONARCH}" STREQUAL "x86")
    set(CMAKE_NASM_LINK_EXECUTABLE "${NEXNIX_TOOLCHAINPREFIX}-gcc")
    if("$ENV{NNARCH}" STREQUAL "i386")
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf32)
    else()
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
    endif()
else()
    set(CMAKE_ASM_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-gcc)
endif()
set(CMAKE_OBJCOPY ${NEXNIX_TOOLCHAINPREFIX}-objcopy)
set(CMAKE_SYSROOT $ENV{NNDESTDIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isystem=${NEXNIX_DESTDIR}/usr/include")

set(CMAKE_FIND_ROOT_PATH $ENV{NNDESTDIR})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

list(APPEND CMAKE_MODULE_PATH "${NEXNIX_DESTDIR}/usr/share/NexNixSdk/systemBuild")
