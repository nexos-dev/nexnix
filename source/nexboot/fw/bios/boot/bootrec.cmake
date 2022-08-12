#[[
    bootrec.cmake - contains helpers to work with boot records
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

include(NasmOverride)

function(add_boot_record __target __source)
    add_executable(${__target} ${__source})
    # Add all link options
    target_link_options(${__target} PUBLIC -T ${CMAKE_CURRENT_SOURCE_DIR}/boot/bootrecLink.ld
                    -Wl,--oformat,binary)
    # Install it
    install(TARGETS ${__target} DESTINATION bootrec)
endfunction()
