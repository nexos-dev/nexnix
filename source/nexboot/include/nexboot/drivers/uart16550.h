/*
    uart16550.h - PC UART stuff
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
*/

#ifndef _UART16550_H
#define _UART16550_H

#include <nexboot/driver.h>
#include <nexboot/fw.h>

typedef struct _uart16550
{
    NbHwDevice_t hdr;
    NbDriver_t* owner;
    uint16_t port;    // Base port of UART
} NbUart16550Dev_t;

#endif
