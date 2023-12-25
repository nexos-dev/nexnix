#[[
    fw.cmake - contains firmware specific build process
    Copyright 2023 The NexNix Project

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

set(NEXBOOT_CFLAGS "-fshort-wchar")

if("${NEXNIX_ARCH}" STREQUAL "x86_64")
    set(NEXBOOT_CFLAGS "${NEXBOOT_CFLAGS} -fpie -fno-pic -fno-plt")
endif()
