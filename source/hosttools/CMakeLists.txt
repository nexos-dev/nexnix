#[[
    CMakeLists.txt - contains host tools build system
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

cmake_minimum_required(VERSION 3.00)
project(hosttools)

# Options
option(TOOLS_ENABLE_TESTS "Specifies if the test suite should be built" OFF)

# Enable testing
if(${TOOLS_ENABLE_TESTS})
    enable_testing()
endif()

# Get the NexNix SDK
find_package(LibNex REQUIRED)

include(GNUInstallDirs)
#include(NexTest)

if(${TOOLS_ENABLE_TESTS})
    #nextest_enable_tests()
endif()

add_subdirectory(confparse)

# Install header files
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/.."
        FILES_MATCHING
        PATTERN "*.h")
