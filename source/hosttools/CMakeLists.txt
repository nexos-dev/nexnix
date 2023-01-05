#[[
    CMakeLists.txt - contains host tools build system
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

cmake_minimum_required(VERSION 3.00)
project(hosttools)

# Options
option(TOOLS_ENABLE_TESTS "Specifies if the test suite should be built" OFF)
option(BUILD_SHARED_LIBS "Specifies if shared libraries should be built" OFF)
option(TOOLS_ENABLE_NLS "Specifies if NLS should be enable in host tools" ON)

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
find_package(LibChardet REQUIRED)
find_package(LibConf REQUIRED)

include(GNUInstallDirs)
include(NexTest)
include(SdkCompilerTest)

# Configure system dependent stuff
check_library_visibility(HAVE_DECLSPEC_EXPORT HAVE_VISIBILITY)
configure_file(include/config.in.h ${CMAKE_BINARY_DIR}/config.h)
include_directories(${CMAKE_BINARY_DIR})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/config.in.h)

add_compile_options(-O0 -g)

# Enable tests if needed
if(${TOOLS_ENABLE_TESTS})
    nextest_enable_tests()
endif()

# Ensure include gets included
include_directories(include)

# Build all subprojects
add_subdirectory(nnbuild)
add_subdirectory(nnimage)
add_subdirectory(nnelf2efi)
