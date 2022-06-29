/*
    conf.c - contains configuration file parser core
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

/// @file conf.c

#include <conf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The name of the file being read
static char* fileName = NULL;

/**
 * @brief Initializes parse
 *
 * Takes a file name and parses the file, return the configuration tree
 * @param file the file to read configuration from
 * @return The root of the parse tree
 */
ConfBlock_t* ConfInit (char* file)
{
    fileName = file;
    return confParse (file);
}

/**
 * @brief Gets the name of the file being worked on
 *
 * @return The file name
 */
char* ConfGetFileName (void)
{
    return fileName;
}

/**
 * Frees all memory associated with parse tree
 */
void ConfFreeParseTree()
{
}
