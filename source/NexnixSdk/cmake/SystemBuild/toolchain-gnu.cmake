#[[
    gnu_toolchain.cmake - contains GNU cross compiliation stuff
    Copyright 2021, 2022, 2023 The NexNix Project

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

list(APPEND CMAKE_MODULE_PATH
            "${CMAKE_SYSROOT}/Programs/SDKs/NexNixSdk/0.0.1/share/NexNixSdk/cmake")

set(NEXNIX_EXETYPE elf)
set(NEXNIX_TOOLCHAINPREFIX $ENV{NNTOOLCHAINPATH}/$ENV{NNARCH}-${NEXNIX_EXETYPE})

set(CMAKE_SYSTEM_NAME NexNix)
set(CMAKE_SYSTEM_VERSION 0.0.1)

set(CMAKE_C_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-g++)
set(CMAKE_AR ${NEXNIX_TOOLCHAINPREFIX}-ar)
set(CMAKE_ASM_COMPILER ${NEXNIX_TOOLCHAINPREFIX}-gcc)
set(CMAKE_OBJCOPY ${NEXNIX_TOOLCHAINPREFIX}-objcopy)

set(CMAKE_C_FLAGS_INIT "-isystem ${CMAKE_SYSROOT}/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${CMAKE_SYSROOT}/usr/include")
set(CMAKE_ASM_FLAGS_INIT "-isystem ${CMAKE_SYSROOT}/usr/include")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
