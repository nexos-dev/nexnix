#[[
    Options.cmake - adds manages options
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

# Defines an option to be passed to the compiler
function(nexnix_add_option __name __help __default)
    # If already defined set option to existing value
    if(DEFINED ${__name})
        option(${__name} ${__help} ${${__name}})

    # Else set to specified default
    else()
        option(${__name} ${__help} ${__default})
    endif()

    # Pass to compiler if defined
    if(${${__name}})
        add_compile_definitions(${__name})
        set_property(GLOBAL APPEND PROPERTY NEXNIX_ARCH_MACROS -D${__name})
    endif()
endfunction()

# Defines an option to be passed to the compiler
function(nexnix_add_parameter __name __help __value)
    # If already defined set option to existing value
    if(DEFINED ${__name})
        set(${__name} ${${__name}} CACHE STRING ${__help} FORCE)

    # Else set to specified default
    else()
        set(${__name} ${__value} CACHE STRING ${__help} FORCE)
    endif()

    # Pass to compiler
    add_compile_definitions(${__name}=${${__name}})
endfunction()
