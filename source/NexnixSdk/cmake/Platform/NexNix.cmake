#[[
    NexNix.cmake - contains NexNix CMake stuff
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

if(NEXNIX)
    return()
endif()

set(NEXNIX 1)

list(APPEND CMAKE_SYSTEM_PREFIX_PATH
    /Programs /System
    /Programs/Index)

list(APPEND CMAKE_SYSTEM_INCLUDE_PATH /Programs/Index/include)
list(APPEND CMAKE_SYSTEM_LIBRARY_PATH /Programs/Index/lib)
list(APPEND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES /Programs/Index/lib /Programs/Index/lib64)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/Programs/${PROJECT_NAME}/${PROJECT_VERSION}")
endif()

include(Platform/UnixPaths)
