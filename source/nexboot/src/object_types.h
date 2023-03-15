/*
    object_types.h - contains object type definitions
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

#ifndef _OBJECT_TYPES_H
#define _OBJECT_TYPES_H

#define NEED_SVC_PTRS

#define OBJ_MAX_TYPES      48
#define OBJ_MAX_INTERFACES 8

// Service table
NbObjSvcTab_t* objSvcTable[OBJ_MAX_TYPES][OBJ_MAX_INTERFACES] = {{&objDirSvcs}};

#endif
