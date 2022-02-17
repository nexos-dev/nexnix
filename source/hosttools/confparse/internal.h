/*
    internal.h - contains internal header
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
*/

/// @file internal.h

#ifndef _INTERNAL_H
#define _INTERNAL_H

#include <conf.h>

/**
 * @brief Internal parser function
 * 
 * confParse read the results from confLex, and then creates a parse tree based on the tokens
 * 
 * @param[in] file the file to parse
 * @return The first block of the file. NULL on error
 */
ConfBlock_t* confParse(char* file);

#endif
