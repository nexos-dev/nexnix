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
#include <stdint.h>

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
    int type;             ///< The type of token that was parsed
    int line;             ///< The line that this token is on
    void* data;           ///< Pointer to some data for the token
    char semVal[1024];    ///< Semantic value of token
    int64_t num;          ///< Numeric value of token
    uint16_t base;        ///< Base of token
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

/**
 * @brief Gets the symbolic name of tok
 * @param tok the token to get the name of
 * @return the name of the token
 */
const char* _confLexGetTokenName (_confToken_t* tok);

// Valid token numbers
#define LEX_TOKEN_NONE          0    ///< No token found
#define LEX_TOKEN_POUND_COMMENT 1    ///< A comment (never returned to users)
#define LEX_TOKEN_SLASH_COMMENT 2
#define LEX_TOKEN_BLOCK_COMMENT 3
#define LEX_TOKEN_OBRACE        4    ///< A left curly brace ({)
#define LEX_TOKEN_EBRACE        5    ///< A right brace (})
#define LEX_TOKEN_COLON         6    ///< Colon token
#define LEX_TOKEN_SEMICOLON     7    ///< Smeicolon token
#define LEX_TOKEN_ID            8    ///< An identifier
#define LEX_TOKEN_NUM           9    ///< A number

#endif
