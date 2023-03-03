#[[
    bootrec.cmake - contains helpers to work with boot records
    Copyright 2022, 2023 The NexNix Project

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

function(add_boot_record __target __source __output __flags)
    add_custom_command(OUTPUT ${__output}
                       COMMAND nasm -f bin ${__source} -o ${__output}
                               -I ${CMAKE_SOURCE_DIR}/fw/bios/include/boot
                               -I ${CMAKE_SYSROOT}/usr/include ${__flags}
                               -D NEXNIX_LOGLEVEL=${NEXNIX_LOGLEVEL}
                               DEPENDS ${__source})
    set_source_files_properties(${__output} PROPERTIES GENERATED TRUE)
    add_custom_target(${__target} ALL DEPENDS ${__output})
    set_target_properties(${__target} PROPERTIES OUTPUT ${__output})
    install(FILES ${__output} DESTINATION ${CMAKE_INSTALL_PREFIX}/bootrec)
endfunction()
