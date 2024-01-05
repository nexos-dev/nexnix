#[[
    CMakeLists.txt - contains CMake build code for nexnix SDK
    Copyright 2022 - 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    There should be a copy of the License distributed in a file named
    LICENSE, if not, you may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License
]]

cmake_minimum_required(VERSION 3.7)
project(NexNixSdk VERSION 0.0.1)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Configure package configuration file
configure_package_config_file(${CMAKE_SOURCE_DIR}/cmake/NexNixSdkConfig.cmake.in
        ${CMAKE_BINARY_DIR}/NexNixSdkConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/cmake
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR)

# Configure version header
configure_file(${CMAKE_SOURCE_DIR}/include/version.h.in ${CMAKE_BINARY_DIR}/version.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/include/version.h.in)

# Install everything
install(DIRECTORY "${CMAKE_SOURCE_DIR}/cmake"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
        FILES_MATCHING
        PATTERN "*.cmake")
install(FILES ${CMAKE_BINARY_DIR}/NexNixSdkConfig.cmake
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/cmake)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/..
        FILES_MATCHING
        PATTERN "*.h")
install(FILES ${CMAKE_BINARY_DIR}/version.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
