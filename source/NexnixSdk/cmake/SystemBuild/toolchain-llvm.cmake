#[[
    llvm_toolchain.cmake - contains LLVM cross compiliation stuff
    Copyright 2021 - 2024 The NexNix Project

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
set(NEXNIX_TOOLCHAINPREFIX $ENV{NNTOOLCHAINPATH})

set(CMAKE_SYSTEM_NAME NexNix)
set(CMAKE_SYSTEM_VERSION 0.0.1)
set(CMAKE_SYSTEM_PROCESSOR $ENV{NNARCH})

set(CMAKE_C_COMPILER ${NEXNIX_TOOLCHAINPREFIX}/clang)
set(CMAKE_CXX_COMPILER ${NEXNIX_TOOLCHAINPREFIX}/clang++)
set(CMAKE_AR ${NEXNIX_TOOLCHAINPREFIX}/llvm-ar)
set(CMAKE_ASM_COMPILER ${NEXNIX_TOOLCHAINPREFIX}/clang)
set(CMAKE_OBJCOPY ${NEXNIX_TOOLCHAINPREFIX}/llvm-objcopy)

if($ENV{NNARCH} STREQUAL "armv8")
    set(NEXNIX_TOOLCHAINARCH "aarch64")
else()
    set(NEXNIX_TOOLCHAINARCH "$ENV{NNARCH}")
endif()

set(CMAKE_C_FLAGS_INIT "--target=${NEXNIX_TOOLCHAINARCH}-${NEXNIX_EXETYPE} -isystem ${CMAKE_SYSROOT}/usr/include")
set(CMAKE_CXX_FLAGS_INIT "--target=${NEXNIX_TOOLCHAINARCH}-${NEXNIX_EXETYPE} -isystem ${CMAKE_SYSROOT}/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -L${CMAKE_SYSROOT}/usr/lib -L$ENV{NNBUILDROOT}/tools/llvm/lib/nexnix -lclang_rt.builtins-${NEXNIX_TOOLCHAINARCH}")
set(CMAKE_ASM_FLAGS_INIT "--target=${NEXNIX_TOOLCHAINARCH}-${NEXNIX_EXETYPE} -isystem ${CMAKE_SYSROOT}/usr/include")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
