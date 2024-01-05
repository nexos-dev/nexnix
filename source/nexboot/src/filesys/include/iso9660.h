/*
    iso9660.h - contains ISO9660 driver
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
*/

#ifndef _ISO9660_H
#define _ISO9660_H

#include <nexboot/nexboot.h>

bool IsoMountFs (NbObject_t* fs);
bool IsoUnmountFs (NbObject_t* fs);
bool IsoOpenFile (NbObject_t* fsObj, NbFile_t* file);
bool IsoCloseFile (NbObject_t* fs, NbFile_t* file);
bool IsoGetFileInfo (NbObject_t* fs, NbFileInfo_t* fileInf);
bool IsoReadFileBlock (NbObject_t* fsObj, NbFile_t* file, uint32_t pos);
bool IsoGetDir (NbObject_t* fsObj, const char* path, NbDirIter_t* iter);
bool IsoReadDir (NbObject_t* fsObj, NbDirIter_t* iter);

#endif
