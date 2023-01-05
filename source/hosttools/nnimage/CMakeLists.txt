#[[
    CMakeLists.txt - contains build system for nnimage
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

cmake_minimum_required(VERSION 3.10)
project(nnimage VERSION 0.0.1)
enable_language(C)

# Add sources
list(APPEND NNIMAGE_SOURCES 
     main.c
     image.c
     imageList.c
     update.c)

# Include libguestfs
find_package(PkgConfig REQUIRED)
pkg_check_modules(GUESTFS REQUIRED IMPORTED_TARGET libguestfs)

# Create program
add_executable(nnimage ${NNIMAGE_SOURCES})
target_link_libraries(nnimage PUBLIC LibConf::conf)

# Set link language to C++ because we indirectly link with libchardet
set_target_properties(nnimage PROPERTIES LINKER_LANGUAGE CXX)

# Link with libguestfs
target_link_libraries(nnimage PRIVATE PkgConfig::GUESTFS)

# Install it
install(TARGETS nnimage)
