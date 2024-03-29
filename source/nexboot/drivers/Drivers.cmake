#[[
    Drivers.cmake - decides what drivers to build
    Copyright 2023 - 2024 The NexNix Project

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

# Append drivers for FW type
if(NEXBOOT_FW STREQUAL "bios")
    list(APPEND NEXBOOT_FW_DRIVERS drivers/vgaconsole.c
        drivers/bioskbd.c
        drivers/uart16550.c
        drivers/biosdisk.c
        drivers/vbe.c)
elseif(NEXBOOT_FW STREQUAL "efi")
    list(APPEND NEXBOOT_FW_DRIVERS drivers/efiserial.c
        drivers/efikbd.c
        drivers/efidisk.c
        drivers/efigop.c)
endif()

list(APPEND NEXBOOT_DRIVERS drivers/terminal.c
    drivers/volmanager.c
    drivers/fbconsole.c)
