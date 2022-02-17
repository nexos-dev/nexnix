#[[
    CMakeLists.txt - contains confparse build system
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
project(confparse)

# Create the library
add_library(conf conf.c)

# Include header stuff
list(APPEND CONF_INCLUDES ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}
            ${CMAKE_SOURCE_DIR}/include)

target_include_directories(conf PRIVATE ${CONF_INCLUDES})

# Install it
install(TARGETS conf)
