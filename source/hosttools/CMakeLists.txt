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
option(BUILD_SHARED_LIBS "Specifies if shared libraries should be built" OFF)

# Enable testing
if(${TOOLS_ENABLE_TESTS} AND NOT ${CMAKE_CROSSCOMPILING})
    enable_testing()
endif()

# Check if we should use the SDK or not
if(CMAKE_SYSTEM_NAME STREQUAL "NexNix")
    find_package(NexNixSdk REQUIRED)
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    include_directories(cmake)
endif()

find_package(LibNex REQUIRED)

include(GNUInstallDirs)
include(NexTest)

if(${TOOLS_ENABLE_TESTS})
    nextest_enable_tests()
endif()

# Ensure include gets included
include_directories(include)

add_subdirectory(confparse)
