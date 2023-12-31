#[[
    arch.cmake - contains build system for nexke armv8
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

# Arch header
set(NEXKE_ARCH_HEADER "${CMAKE_SOURCE_DIR}/include/nexke/cpu/armv8/armv8.h")

# Set linker script
set(NEXKE_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cpu/armv8/link.ld")

list(APPEND NEXKE_SOURCES "cpu/armv8/cpudep.c")
