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

#include <libnex/libnex_config.h>

#define MAX_PROPVAR 64    // The maximum amount of values in a property

#define DATATYPE_STRING 0    ///< Value of property is a string
#define DATATYPE_QUOTE  1    ///< Value of property is a literal string
#define DATATYPE_NUMBER 2    ///< Value of property is a number

///< The value of a property
typedef struct tagPropertyValue
{
    int lineNo;    ///< The line number of this property value
    union          ///< The value of this property
    {
        char* val;     ///< A string
        int numVal;    ///< ... or a number
    };
    int type;    ///< 0 = string, 1 = quoted, 2 = numeric
} ConfPropVal_t;

/// A property. Properties are what define characteristics of what is being configured
typedef struct tagProperty
{
    int lineNo;                         ///< The line number of this property declaration
    char* name;                         ///< The property represented here
    ConfPropVal_t vals[MAX_PROPVAR];    ///< 64 comma seperated values
    int nextVal;                        ///< The next value to work with
    struct tagProperty* next;
} ConfProperty_t;

/**
 * @brief Contains a block for the parse tree
 *
 * A block is the top level data structure in confparse. It contains information about individual keys
 */
typedef struct tagBlock
{
    int lineNo;                 ///< The line number of this block declaration in the source file
    char* blockType;            ///< What this block covers
    char* blockName;            ///< The name of this block
    ConfProperty_t* props;      ///< The list of properties associated with this block
    ConfProperty_t* curProp;    ///< The current property that is being worked on
    struct tagBlock* next;      ///< Pointer to next block
} ConfBlock_t;

/**
 * @brief Gets the name of the file being worked on
 * @return The file name
 */
PUBLIC char* ConfGetFileName (void);

/**
 * @brief Initializes configuration context
 * Takes a file name and parses the file, and returns the parse tree
 * @param file the file to read configuration from
 * @return The root of the parse tree
 */
PUBLIC ConfBlock_t* ConfInit (char* file);

/**
 * @brief Frees all memory associated with parse tree
 */
PUBLIC void ConfFreeParseTree();

#endif
