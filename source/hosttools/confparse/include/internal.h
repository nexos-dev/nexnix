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
#include <config.h>
#include <libintl.h>

#ifdef TOOLS_ENABLE_NLS
#define _(str)  gettext (str)
#define N_(str) (str)
#else
#define _(str)  (str)
#define N_(str) (str)
#endif

/// Specifies a token that was parsed by the lexer
typedef struct _confToken
{
    int type;        ///< The type of token that was parsed
    char* semVal;    ///< Semantic value of the token
    int line;        ///< The line that this token is on
} _confToken_t;

/**
 * @brief Internal parser function
 *
 * _confParse reads the results from _confLex, and then creates a parse tree based on the tokens
 *
 * @param[in] file the file to parse
 * @return The first block of the file. NULL on error
 */
ConfBlock_t* _confParse (const char* file);

/**
 * @brief Initializes the lexer
 * @param file the file to lex
 * @return 1 on success, 0 otherwise
 */
void _confLexInit (const char* file);

/**
 * @brief Destroys the lexer
 */
void _confLexDestroy (void);

/**
 * @brief Lexes a token
 * @return The token. NULL if an error ocurred
 */
_confToken_t* _confLex (void);

/**
 * @brief Peeks at a token, but doesn't advance in the buffer
 * @return The token. NULL if an error ocurred
 */
_confToken_t* _confLexPeek (void);

// Valid token numbers
#define LEX_TOKEN_NONE          0    ///< No token found
#define LEX_TOKEN_POUND_COMMENT 1    ///< A comment (never returned to users)
#define LEX_TOKEN_SLASH_COMMENT 2
#define LEX_TOKEN_BLOCK_COMMENT 3

#endif
