/*
    conf.h - contains configuration file parser header
    Copyright 2021, 2022 The NexNix Project

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

/// @file conf.h

#ifndef CONF_H
#define CONF_H

#include <config.h>
#include <libnex/char32.h>
#include <libnex/list.h>

#define MAX_PROPVAR 64    // The maximum amount of values in a property

#define DATATYPE_IDENTIFIER 0    ///< Value of property is a identifier
#define DATATYPE_STRING     1    ///< Value of property is a string
#define DATATYPE_NUMBER     2    ///< Value of property is a number

#define BLOCK_BUFSZ 512

///< The value of a property
typedef struct tagPropertyValue
{
    int lineNo;    ///< The line number of this property value
    union          ///< The value of this property
    {
        char id[BLOCK_BUFSZ];         ///< An identifier
        char32_t str[BLOCK_BUFSZ];    /// ... or a string
        int64_t numVal;               ///< ... or a number
    };
    int type;    ///< 0 = identifier, 1 = string, 2 = numeric
} ConfPropVal_t;

/// A property. Properties are what define characteristics of what is being configured
typedef struct tagProperty
{
    int lineNo;                         ///< The line number of this property declaration
    char name[BLOCK_BUFSZ];             ///< The property represented here
    ConfPropVal_t vals[MAX_PROPVAR];    ///< 64 comma seperated values
    int nextVal;                        ///< The next value to work with
} ConfProperty_t;

/**
 * @brief Contains a block for the parse tree
 *
 * A block is the top level data structure in confparse. It contains information about individual keys
 */

typedef struct tagBlock
{
    int lineNo;                     ///< The line number of this block declaration in the source file
    char blockType[BLOCK_BUFSZ];    ///< What this block specifies
    char blockName[BLOCK_BUFSZ];    ///< The name of this block
    ListHead_t* props;              ///< The list of properties associated with this block
} ConfBlock_t;

/**
 * @brief Gets the name of the file being worked on
 * @return The file name
 */
PUBLIC const char* ConfGetFileName (void);

/**
 * @brief Initializes configuration context
 * Takes a file name and parses the file, and returns the parse list
 * @param file the file to read configuration from
 * @return The list of blocks
 */
PUBLIC ListHead_t* ConfInit (const char* file);

/**
 * @brief Frees all memory associated with parse tree
 */
PUBLIC void ConfFreeParseTree();

#endif
