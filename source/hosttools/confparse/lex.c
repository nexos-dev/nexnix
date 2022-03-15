/*
    lex.c - contains lexer for confparse
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

/// @file lex.c

#include "include/internal.h"
#include <assert.h>
#include <chardet/chardet.h>
#include <errno.h>
#include <libnex/error.h>
#include <libnex/object.h>
#include <libnex/progname.h>
#include <libnex/safemalloc.h>
#include <libnex/textstream.h>
#include <stdlib.h>

#define LEX_FRAME_SZ 2048    // Size of lexing staging buffer

// Valid error state for lexer
#define LEX_ERROR_NONE             0
#define LEX_ERROR_UNEXPECTED_TOKEN 1
#define LEX_ERROR_UNKNOWN_TOKEN    2
#define LEX_ERROR_UNEXPECTED_EOF   3

// Helper function macros
#define CHECK_NEWLINE               \
    if (curChar == '\n')            \
    {                               \
        ++state.line;               \
        tok->type = LEX_TOKEN_NONE; \
        break;                      \
    }                               \
    else if (curChar == '\r')       \
    {                               \
        if (_lexPeekChar() == '\n') \
            _lexSkipChar();         \
        ++state.line;               \
        tok->type = LEX_TOKEN_NONE; \
        break;                      \
    }

// The state of the lexer
typedef struct _lexState
{
    TextStream_t* stream;    // Text stream object
    // Base state of lexer
    bool isEof;          // Is the lexer at the end of the file?
    bool isAccepted;     // Is the current token accepted?
    _confToken_t tok;    // Current token
    // Diagnostic data
    int line;            // Line number in lexer
    char32_t curChar;    // Current character
    // Peek releated information
    char32_t nextChar;    // Contains the next character. If the read functions find this set, then they use this
                          // instead
} lexState_t;

lexState_t state = {0};

void _confLexInit (const char* file)
{
    assert (file);
    // Detect character set
    DetectObj* obj = detect_obj_init();
    short res = 0;
    if ((res = detect_file (file, 8192, &obj)) != CHARDET_SUCCESS)
    {
        if (res == CHARDET_IO_ERROR)
            error (strerror (errno));
    }
    char enc = 0, order = 0;
    TextGetEncId (obj->encoding, &enc, &order);
    // Open up the text stream
    res = TextOpen (file, &state.stream, TEXT_MODE_READ, enc, obj->bom, order);
    if (res != TEXT_SUCCESS)
        error ("%s", TextError (res));
    // Set up state
    state.line = 1;
    // Free stuff we're done with
    detect_obj_free (&obj);
}

void _confLexDestroy()
{
    assert (state.stream);
    TextClose (state.stream);
}

// Gets the name of a token
char* _confLexGetTokenName (_confToken_t* tok)
{
    switch (tok->type)
    {
        case LEX_TOKEN_POUND_COMMENT:
            return "'#'";
        case LEX_TOKEN_SLASH_COMMENT:
            return "'//'";
        case LEX_TOKEN_BLOCK_COMMENT:
            return "'/* ... */";
        case LEX_TOKEN_NONE:
        default:
            return "";
    }
}

// Prints out an error condition
static inline void _lexError (int err)
{
    mbstate_t mbState;
    memset (&mbState, 0, sizeof (mbstate_t));
    size_t mbBytesWritten = 0;

    char extraBuf[5];
    char bufData[1024];

    char* obuf = bufData;
    char* buf = bufData;

    buf += snprintf (buf, 1024 - (buf - obuf), _ ("error: %s:"), ConfGetFileName());
    buf += snprintf (buf, 1024 - (buf - obuf), "%d: ", state.line);
    // Decide how to handle the error
    switch (err)
    {
        case LEX_ERROR_UNEXPECTED_TOKEN:
            // Print out error string
            buf += snprintf (buf, 1024 - (buf - obuf), _ ("Unexpected token "));
            buf += snprintf (buf, 1024 - (buf - obuf), "%s", _confLexGetTokenName (&state.tok));
            break;
        case LEX_ERROR_UNKNOWN_TOKEN:
            // Convert current char to a char
            mbBytesWritten = c32rtomb (extraBuf, state.curChar, &mbState);
            if (mbBytesWritten == -1)
                error (_ ("internal error: %s"), strerror (errno));
            extraBuf[mbBytesWritten] = '\0';
            // Add everything
            buf += snprintf (buf, 1024 - (buf - obuf), _ ("Unknown token '%s'"), extraBuf);
            break;
        case LEX_ERROR_UNEXPECTED_EOF:
            // Print out string
            buf += snprintf (buf, 1024 - (buf - obuf), _ ("Unexpected EOF"));
            break;
    }
    error (obuf);
}

// Reads a character from the file
static inline char32_t _lexReadChar()
{
    char32_t c = 0;
    short res = 0;
    // Check if state.nextChar is set
    if (state.nextChar)
    {
        c = state.nextChar;
        // Reset it so we know to advance
        state.nextChar = 0;
    }
    else
    {
        // Read in the character
        if ((res = TextReadChar (state.stream, &c)) != TEXT_SUCCESS)
            error (_ ("internal error: %s"), TextError (res));
        // Check for end of file
        if (TextIsEof (state.stream))
            return '\0';
    }
    state.curChar = c;
    return c;
}

// Peek at the next character in the file
static inline char32_t _lexPeekChar()
{
    char32_t c = 0;
    short res = 0;
    // Check if nextChar is set
    if (state.nextChar)
        c = state.nextChar;
    else
    {
        // Read in a character, setting nextChar
        if ((res = TextReadChar (state.stream, &c)) != TEXT_SUCCESS)
            error (_ ("internal error: %s"), TextError (res));
        // Check for end of file
        if (TextIsEof (state.stream))
            return '\0';
        state.nextChar = c;
    }
    return c;
}

// Skips over a character that was peeked at
#define _lexSkipChar() (state.nextChar = 0)

// Internal lexer. VERY performance critical, please try to keep additions to a minimum
_confToken_t* _lexInternal()
{
    assert (state.stream);
    // If we're at the end of the file, report it
    if (state.isEof)
        return NULL;
    _confToken_t* tok = &state.tok;
    int prevType = 0;
    // Reset token state
    tok->type = LEX_TOKEN_NONE;
    state.isAccepted = false;
    while (!state.isAccepted)
    {
        // Read in a character
        char32_t curChar = _lexReadChar();
        // Decide what to do with this character
        switch (curChar)
        {
            case '\0':
                // Unconditionally accept on EOF
                state.isAccepted = true;
                break;
            case ' ':
            case '\v':
            case '\f':
            case '\t':
                // A seperator. Seperators mark the acceptance of a token, as long as there is a token to accept
                if (tok->type != LEX_TOKEN_NONE)
                    state.isAccepted = true;
                break;
            case '\r':
                // Carriage return. Can be Mac style (CR alone) or DOS style (CR followed by LF)
                // Look for an LF in case this is DOS style
                if (_lexPeekChar() == '\n')
                    _lexSkipChar();
            // fall through
            case '\n':
                // Line feed. Increment current line and accept current token.
                ++state.line;
                // Accept if there is a token to accept
                if (tok->type != LEX_TOKEN_NONE)
                    state.isAccepted = true;
                break;
            case '#':
                // Comment starting with a pound
                prevType = tok->type;
                tok->type = LEX_TOKEN_POUND_COMMENT;
                goto lexComment;
            case '/':
                // Check for a single line comment
                if (_lexPeekChar() == '/')
                {
                    _lexSkipChar();
                    prevType = tok->type;
                    tok->type = LEX_TOKEN_SLASH_COMMENT;
                    goto lexComment;
                }
                // Maybe a block comment?
                else if (_lexPeekChar() == '*')
                {
                    _lexSkipChar();
                    prevType = tok->type;
                    tok->type = LEX_TOKEN_BLOCK_COMMENT;
                    // Ensure this is in a valid spot
                    if (prevType != LEX_TOKEN_NONE)
                        _lexError (LEX_ERROR_UNEXPECTED_TOKEN);
                // Lex it
                lexBlockComment:
                    curChar = _lexReadChar();
                    if (curChar == '*')
                    {
                        // Look for a '/'
                        if (_lexPeekChar() == '/')
                        {
                            // End of comment
                            _lexSkipChar();
                            break;
                        }
                    }
                    else if (curChar == '\r')
                    {
                        // Still increment line
                        if (_lexPeekChar() == '\n')
                            _lexSkipChar();
                        ++state.line;
                    }
                    else if (curChar == '\n')
                        ++state.line;
                    else if (curChar == '\0')
                    {
                        // Unexpected EOF
                        _lexError (LEX_ERROR_UNEXPECTED_EOF);
                    }
                    goto lexBlockComment;
                }
                goto unkownToken;
            // Fall through
            lexComment:
                // This is a comment (maybe). If this is in the middle of another token, then that's an error
                if (prevType != LEX_TOKEN_NONE)
                    _lexError (LEX_ERROR_UNEXPECTED_TOKEN);
                // Iterate through the comment
                curChar = _lexReadChar();
                // Check for a newline
                CHECK_NEWLINE
                // Check for newline
                goto lexComment;
            unkownToken:
            default:
                // An error here
                _lexError (LEX_ERROR_UNKNOWN_TOKEN);
        }
    }
    return &state.tok;
}

// Lexer entry points
_confToken_t* _confLex()
{
    // Just lex and return
    return _lexInternal();
}
