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

include(GNUInstallDirs)
include(NexTest)
include(SdkCompilerTest)
include(GettextHelp)

# Find libintl
find_package(Intl)
if(NOT Intl_FOUND)
    message(WARNING "libintl not found, compiling without NLS (multilingual) support")
    set(TOOLS_ENABLE_NLS OFF)
endif()

# Find GNU gettext
gettext_check(GETTEXT_FOUND)
if(NOT GETTEXT_FOUND)
    message(WARNING "GNU gettext not found, compiling without NLS")
    set(TOOLS_ENABLE_NLS OFF)
else()
    message(STATUS "Found GNU gettext")
endif()

if(TOOLS_ENABLE_NLS)
    # Find the locale directory base
    set(TOOLS_LOCALE_BASE "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/locale/")
    # Check which locales should be enabled
    set(TOOLS_SUPPORTED_LOCALES "en;es" CACHE STRING "Languages supported by host tools")
    if(NOT TOOLS_ENABLED_LOCALES)
        set(TOOLS_ENABLED_LOCALES "${TOOLS_SUPPORTED_LOCALES}" CACHE STRING "Languages to use in host tools")
    endif()
    # Validate locales
    foreach(locale ${TOOLS_ENABLED_LOCALES})
        # Strip en from list. en_US is the locale that the message IDs are in, meaning that we
        # don't need a l10n for that
        if("${locale}" STREQUAL "en")
            list(REMOVE_ITEM TOOLS_ENABLED_LOCALES "en")
        endif()
        list(FIND TOOLS_SUPPORTED_LOCALES ${locale} locale_supported)
        if("${locale_supported}" STREQUAL "-1")
            message(FATAL_ERROR "Locale ${locale} not supported")
        endif()
    endforeach()
endif()

# Configure system dependent stuff
check_library_visibility(HAVE_DECLSPEC_EXPORT HAVE_VISIBILITY)
configure_file(include/config.in.h ${CMAKE_BINARY_DIR}/config.h)
include_directories(${CMAKE_BINARY_DIR})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/config.in.h)

# Enable tests if needed
if(${TOOLS_ENABLE_TESTS})
    nextest_enable_tests()
endif()

# Ensure include gets included
include_directories(include)

# Build all subprojects
add_subdirectory(confparse)
