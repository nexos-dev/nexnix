/*
    fat.h - contains FAT driver
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

#ifndef _FAT_H
#define _FAT_H

#include <nexboot/nexboot.h>

bool FatMountFs (NbObject_t* fs);
bool FatUnmountFs (NbObject_t* fs);
bool FatOpenFile (NbObject_t* fsObj, NbFile_t* file);
bool FatCloseFile (NbObject_t* fs, NbFile_t* file);
bool FatGetFileInfo (NbObject_t* fs, NbFileInfo_t* fileInf);
bool FatReadFileBlock (NbObject_t* fsObj, NbFile_t* file, uint32_t pos);
bool FatGetDir (NbObject_t* fsObj, const char* path, NbDirIter_t* iter);
bool FatReadDir (NbObject_t* fsObj, NbDirIter_t* iter);

#endif
