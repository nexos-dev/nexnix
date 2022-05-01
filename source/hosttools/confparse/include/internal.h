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
#include <libnex/char32.h>
#include <libnex/list.h>
#include <libnex/textstream.h>
#include <stdbool.h>
#include <stdint.h>

#define TOK_SEM_SIZE 2048

/// Specifies a token that was parsed by the lexer
typedef struct _confToken
{
    int type;      ///< The type of token that was parsed
    int line;      ///< The line that this token is on
    void* data;    ///< Pointer to some data for the token
    union
    {
        char semVal[TOK_SEM_SIZE];        ///< Semantic value of token
        char32_t strVal[TOK_SEM_SIZE];    ///< Size of string used to store strings
    };
    int64_t num;      ///< Numeric value of token
    uint16_t base;    ///< Base of token
} _confToken_t;

/// The state of the lexer
typedef struct _lexState
{
    TextStream_t* stream;    ///< Text stream object
    // Base state of lexer
    bool isEof;           ///< Is the lexer at the end of the file?
    bool isAccepted;      ///< Is the current token accepted?
    _confToken_t* tok;    ///< Current token
    // Diagnostic data
    int line;            ///< Line number in lexer
    char32_t curChar;    ///< Current character
    // Peek releated information
    char32_t nextChar;    ///< Contains the next character. If the read functions find this set, then they use this
                          /// instead
    int loc;              ///< Location in states table
} lexState_t;

/**
 * @brief Internal parser function
 *
 * _confParse reads the results from _confLex, and then creates a parse tree based on the tokens
 *
 * @param[in] file the file to parse
 * @return The list of blocks in the file
 */
ListHead_t* _confParse (const char* file);

/**
 * @brief Initializes the lexer
 * @param file the file to lex
 * @return The lexer's state
 */
lexState_t* _confLexInit (const char* file);

/**
 * @brief Destroys the lexer
 * @param state the lexer to destroy
 */
void _confLexDestroy (lexState_t* state);

/**
 * @brief Lexes a token
 * @param state the lexer to lex
 * @return The token. NULL if an error ocurred
 */
_confToken_t* _confLex (lexState_t* state);

/**
 * @brief Gets the symbolic name of tok
 * @param tok the token to get the name of
 * @return the name of the token
 */
const char* _confLexGetTokenName (_confToken_t* tok);

/**
 * @brief Gets the name associated with type
 * @param type the type to get the name of
 * @return the name of the type
 */
const char* _confLexGetTokenNameType (int type);

// Valid token numbers
#define LEX_TOKEN_NONE          0    ///< No token found
#define LEX_TOKEN_POUND_COMMENT 1    ///< A comment (never returned to users)
#define LEX_TOKEN_SLASH_COMMENT 2
#define LEX_TOKEN_BLOCK_COMMENT 3
#define LEX_TOKEN_OBRACE        4     ///< A left curly brace ({)
#define LEX_TOKEN_EBRACE        5     ///< A right brace (})
#define LEX_TOKEN_COLON         6     ///< Colon token
#define LEX_TOKEN_SEMICOLON     7     ///< Smeicolon token
#define LEX_TOKEN_ID            8     ///< An identifier
#define LEX_TOKEN_NUM           9     ///< A number
#define LEX_TOKEN_STR           11    ///< A string
#define LEX_TOKEN_INCLUDE       12    ///< Keyword include
#define LEX_TOKEN_EOF           13    ///< End of file
#define LEX_TOKEN_COMMA         14    ///< A comma
#define LEX_TOKEN_ERROR         15    ///< Error condition

#endif
