#[[
    ArchToMacro.cmake - adds compiler definitions for architecture variables
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
    limitations under the License.
]]

list(APPEND __nexnixVars NEXNIX_ARCH NEXNIX_BOARD NEXNIX_TARGETCONF
    NEXNIX_BASEARCH NEXNIX_TOOLCHAIN)

# Expand to a defitition
foreach(var ${__nexnixVars})
    set(val ${${var}}) # Get value of variable
    string(TOUPPER ${val} upperVal) # Convert to uppercase
    string(REPLACE "-" "_" finalVal ${upperVal})
    add_compile_definitions(${var}_${finalVal}) # Add to definitions
    set_property(GLOBAL APPEND PROPERTY NEXNIX_ARCH_MACROS -D${var}_${finalVal})
endforeach()
