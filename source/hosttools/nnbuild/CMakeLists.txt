#[[
    CMakeLists.txt - contains build system for nnbuild
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

cmake_minimum_required(VERSION 3.00)
project(nnbuild VERSION 0.0.1)
enable_language(C)

# Add sources
list(APPEND NNBUILD_SOURCES main.c package.c build.c)

# Create the PO file lists
if(TOOLS_ENABLE_NLS)
    gettext_files(DOMAIN nnbuild
                  OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/po" 
                  LANGS "${TOOLS_ENABLED_LOCALES}"
                  SOURCES "${NNBUILD_SOURCES}")
endif()

# Create program
add_executable(nnbuild ${NNBUILD_SOURCES})

# Set link language to C++ because we indirectly link with libchardet
set_target_properties(nnbuild PROPERTIES LINKER_LANGUAGE CXX)

# Link with libconf
target_link_libraries(nnbuild PRIVATE conf)

# Install it
install(TARGETS nnbuild)
