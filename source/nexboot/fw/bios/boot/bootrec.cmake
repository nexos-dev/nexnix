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

function(add_boot_record __target __source __isStage1)
    add_executable(${__target} ${__source})
    # Add all link options
    if(${__isStage1})
        set(__link_script "${CMAKE_CURRENT_SOURCE_DIR}/boot/bootrecLinkStage1.ld")
    else()
        set(__link_script "${CMAKE_CURRENT_SOURCE_DIR}/boot/bootrecLinkStage2.ld")
    endif()
    # Ensure code knows toolchain being used
    if(${NEXNIX_TOOLCHAIN} STREQUAL "gnu")
        target_compile_definitions(${__target} PUBLIC TOOLCHAIN_GNU)
    endif()
    target_link_options(${__target} PUBLIC -T ${__link_script} -Wl,--oformat,binary -z notext)
    target_include_directories(${__target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/boot)
    # Install it
    install(TARGETS ${__target} DESTINATION bootrec)
endfunction()
