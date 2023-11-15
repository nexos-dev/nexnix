/*
    conf.h - contains conf data structures
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

#ifndef _CONF_H
#define _CONF_H

#include <libnex/list.h>
#include <libnex/stringref.h>
#include <nexboot/vfs.h>

// Internal lexer stuff
typedef struct _conftoken
{
    int type;               ///< The type of token that was parsed
    int line;               ///< The line that this token is on
    StringRef_t* semVal;    ///< Semantic value of token
} confToken_t;

// Valid token numbers
#define LEX_TOKEN_NONE      0    // No token found
#define LEX_TOKEN_COMMENT   1    // A comment (never returned to users)
#define LEX_TOKEN_OBRACE    4    // A left curly brace ({)
#define LEX_TOKEN_EBRACE    5    // A right brace (})
#define LEX_TOKEN_DOLLAR    6    // A dollar sign
#define LEX_TOKEN_ID        8    // An identifier
#define LEX_TOKEN_NEWLINE   9
#define LEX_TOKEN_LITERAL   10    // A literal
#define LEX_TOKEN_STR       11    // A string
#define LEX_TOKEN_SET       12    // Keyword set
#define LEX_TOKEN_EOF       13    // End of file
#define LEX_TOKEN_ERROR     15    // Error condition
#define LEX_TOKEN_MENUENTRY 16

// Lexer state
typedef struct _lexstate
{
    // Base state of lexer
    bool isEof;          // Is the lexer at the end of the file?
    bool isAccepted;     // Is the current token accepted?
    confToken_t* tok;    // Current token
    // Diagnostic data
    int line;        // Line number in lexer
    char curChar;    // Current character
    // Peek releated information
    char nextChar;       // Contains the next character. If the read functions
                         // find this set, then they use this
                         // instead
    StringRef_t* buf;    // Line / file buffer
    size_t bufSz;        // Size of buffer
    int bufPos;          // Current position in buffer
} confLexState_t;

// Context for configuration parsing
typedef struct _confctx
{
    bool isFile;    // If file is being parsed
    union
    {
        NbFile_t* confFile;    // File to parse
        struct
        {
            StringRef_t* line;                          // Line of text
            void (*readCallback) (struct _confctx*);    // If we need another line
                                                        // of text
            size_t bufSz;                               // Size of line buffer
            size_t maxBufSz;                            // Max size of line buffer
        };
    };
    confLexState_t lexer;
    // Parser state
    confToken_t* lastToken;    // For better diagnostics
    ListHead_t* blocks;        // List of parsed blocks
    bool insideMenu;           // If we are parsing a menu entry
} ConfContext_t;

// Initializes lexer
bool confLexInit (ConfContext_t* ctx);

// Destroys lexer
bool confLexDestroy (ConfContext_t* ctx);

// Lexes a token
confToken_t* confLex (ConfContext_t* ctx);

// Gets name of tok
const char* confGetTokName (confToken_t* tok);

typedef struct _confblock
{
    int lineNo;    // Line number of block
    int type;      // Type of block
} ConfBlock_t;

#define CONF_BLOCK_CMDARG    0
#define CONF_BLOCK_MENUENTRY 1
#define CONF_BLOCK_VARSET    2
#define CONF_BLOCK_CMD       3

// General string structure
typedef struct _confstring
{
    int type;    // Type of string
    union
    {
        StringRef_t* var;
        StringRef_t* literal;
    };
} ConfString_t;

#define CONF_STRING_LITERAL 1
#define CONF_STRING_VAR     2

// Block types
typedef struct _blockcmdarg
{
    ConfBlock_t hdr;     // Block header
    ConfString_t str;    // String of argument
} ConfBlockCmdArg_t;

typedef struct _varset
{
    ConfBlock_t hdr;
    StringRef_t* var;    // Variable to set
    ConfString_t val;    // Value to set
} ConfBlockSet_t;

typedef struct _blockmenu
{
    ConfBlock_t hdr;
    StringRef_t* name;     // Entry name
    ListHead_t* blocks;    // Blocks in this entry
} ConfBlockMenu_t;

typedef struct _blockcmd
{
    ConfBlock_t hdr;
    ConfString_t cmd;    // Command to execute
    ListHead_t* args;    // Argument list
} ConfBlockCmd_t;

// Performs lexing / parsing
ListHead_t* NbConfParse (ConfContext_t* ctx);

#endif
