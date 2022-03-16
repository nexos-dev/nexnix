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
#include <limits.h>
#include <stdlib.h>

#define LEX_FRAME_SZ 2048    // Size of lexing staging buffer

// Valid error state for lexer
#define LEX_ERROR_NONE             0
#define LEX_ERROR_UNEXPECTED_TOKEN 1
#define LEX_ERROR_UNKNOWN_TOKEN    2
#define LEX_ERROR_UNEXPECTED_EOF   3
#define LEX_ERROR_INTERNAL         4

// Helper function macros
#define CHECK_NEWLINE_BREAK         \
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

#define CHECK_NEWLINE               \
    if (curChar == '\n')            \
    {                               \
        ++state.line;               \
        tok->type = LEX_TOKEN_NONE; \
    }                               \
    else if (curChar == '\r')       \
    {                               \
        if (_lexPeekChar() == '\n') \
            _lexSkipChar();         \
        ++state.line;               \
        tok->type = LEX_TOKEN_NONE; \
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

// Prints out an error condition
static inline void _lexError (int err, const char* extra)
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
        case LEX_ERROR_INTERNAL:
            buf += snprintf (buf, 1024 - (buf - obuf), _ ("internal error: %s"), extra);
            break;
    }
    // Silence clang-tidy warnings about buf being unused
    (void) buf;
    error (obuf);
}

void _confLexInit (const char* file)
{
    assert (file);
    // Detect character set
    DetectObj* obj = detect_obj_init();
    short res = 0;
    if ((res = detect_file (file, 8192, &obj)) != CHARDET_SUCCESS)
    {
        if (res == CHARDET_IO_ERROR)
            _lexError (LEX_ERROR_INTERNAL, strerror (errno));
    }
    char enc = 0, order = 0;
    TextGetEncId (obj->encoding, &enc, &order);
    // Open up the text stream
    res = TextOpen (file, &state.stream, TEXT_MODE_READ, enc, obj->bom, order);
    if (res != TEXT_SUCCESS)
        _lexError (LEX_ERROR_INTERNAL, TextError (res));
    // Set up state
    state.line = 1;
    // Free stuff we're done with
    detect_obj_free (&obj);
    // Insure that things get cleaned up
    atexit (_confLexDestroy);
}

void _confLexDestroy()
{
    if (state.stream)
        TextClose (state.stream);
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
            _lexError (LEX_ERROR_INTERNAL, TextError (res));
        // Check for end of file
        if (TextIsEof (state.stream))
        {
            state.isEof = 1;
            return '\0';
        }
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
            _lexError (LEX_ERROR_INTERNAL, TextError (res));
        // Check for end of file
        if (TextIsEof (state.stream))
        {
            state.isEof = 1;
            return '\0';
        }
        state.nextChar = c;
    }
    return c;
}

// Returns a character to the buffer
#define _lexReturnChar(c) (state.nextChar = (c));

// Skips over a character that was peeked at
#define _lexSkipChar() (state.nextChar = 0)

// Checks if the current character is whitespace
static inline bool _lexIsSpace (char32_t c)
{
    switch (c)
    {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\v':
        case '\f':
            return true;
        default:
            return false;
    }
}

// Checks if the current character is numeric
static inline bool _lexIsNumeric (char32_t c)
{
    switch (c)
    {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return true;
        default:
            return false;
    }
}

// Checks if the current character is a valid ID character
static inline bool _lexIsIdChar (char32_t c)
{
    switch (c)
    {
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '_':
        case '-':
            return true;
        default:
            return false;
    }
}

// Internal lexer. VERY performance critical, please try to keep additions to a minimum
_confToken_t* _lexInternal()
{
    assert (state.stream);
    // If we're at the end of the file, report it
    if (state.isEof)
        return NULL;
    _confToken_t* tok = &state.tok;
    int prevType = 0;
    int bufPos = 0;
    int numBufPos = 0;
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
                break;
            case '\r':
                // Carriage return. Can be Mac style (CR alone) or DOS style (CR followed by LF)
                // Look for an LF in case this is DOS style
                if (_lexPeekChar() == '\n')
                    _lexSkipChar();
            // fall through
            case '\n':
                // Line feed. Increment current line
                ++state.line;
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
                        _lexError (LEX_ERROR_UNEXPECTED_TOKEN, NULL);
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
                        _lexError (LEX_ERROR_UNEXPECTED_EOF, NULL);
                    }
                    goto lexBlockComment;
                }
                else
                    goto unkownToken;
            // Fall through
            lexComment:
                // This is a comment (maybe). If this is in the middle of another token, then that's an error
                if (prevType != LEX_TOKEN_NONE)
                    _lexError (LEX_ERROR_UNEXPECTED_TOKEN, NULL);
                // Iterate through the comment
                curChar = _lexReadChar();
                // Check for a newline
                CHECK_NEWLINE_BREAK
                // Check for newline
                goto lexComment;
            // Lex single character identifiers
            case '{':
                // Prepare token
                tok->type = LEX_TOKEN_OBRACE;
                tok->line = state.line;
                // Accept it
                state.isAccepted = true;
                break;
            case '}':
                // Prepare it
                tok->type = LEX_TOKEN_EBRACE;
                tok->line = state.line;
                // Accept
                state.isAccepted = true;
                break;
            case ':':
                // Prepare it
                tok->type = LEX_TOKEN_COLON;
                tok->line = state.line;
                // Accept
                state.isAccepted = true;
                break;
            case ';':
                // Prepare and accept
                tok->type = LEX_TOKEN_SEMICOLON;
                tok->line = state.line;
                state.isAccepted = true;
                break;
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
            case 'i':
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 'o':
            case 'p':
            case 'q':
            case 'r':
            case 's':
            case 't':
            case 'u':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
            case 'z':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'M':
            case 'N':
            case 'O':
            case 'P':
            case 'Q':
            case 'R':
            case 'S':
            case 'T':
            case 'U':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'Z':
            case '_':
                // Prepare it
                tok->type = LEX_TOKEN_ID;
                tok->line = state.line;
                // Add the rest of it
                while (_lexIsIdChar (curChar))
                {
                    tok->semVal[bufPos] = (char) curChar;
                    ++bufPos;
                    curChar = _lexReadChar();
                }
                // Null terminate it
                tok->semVal[bufPos] = 0;
                // Return character to buffer
                _lexReturnChar (curChar);
                // Accept
                state.isAccepted = true;
                break;
            case '0':
                // Figure out base when a number starts with 0
                if (_lexPeekChar() == 'x')
                {
                    // Skip and set base
                    _lexSkipChar();
                    tok->base = 16;
                }
                else if (_lexPeekChar() == 'b')
                {
                    // Same thing
                    _lexSkipChar();
                    tok->base = 2;
                }
                else
                    tok->base = 8;
                curChar = _lexReadChar();
                goto lexNum;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
                tok->base = 10;
            lexNum:
                // Prepare the token
                tok->type = LEX_TOKEN_NUM;
                tok->line = state.line;
                // Add rest of value
                while (_lexIsNumeric (curChar) || (bufPos == 0 && curChar == '-'))
                {
                    tok->semVal[bufPos] = (char) curChar;
                    ++bufPos;
                    curChar = _lexReadChar();
                }
                // Null terminate
                tok->semVal[bufPos] = 0;
                // Return first non-numeric character
                _lexReturnChar (curChar);
                // Convert the string to numeric
                tok->num = strtoll (tok->semVal, NULL, tok->base);
                if (tok->num == LONG_MIN || tok->num == LONG_MAX)
                    _lexError (LEX_ERROR_INTERNAL, strerror (errno));
                // Accept it
                state.isAccepted = true;
                break;
            unkownToken:
            default:
                // An error here
                _lexError (LEX_ERROR_UNKNOWN_TOKEN, NULL);
        }
    }
    return &state.tok;
}

const char* _confLexGetTokenName (_confToken_t* tok)
{
    switch (tok->type)
    {
        case LEX_TOKEN_POUND_COMMENT:
            return "'#'";
        case LEX_TOKEN_SLASH_COMMENT:
            return "'//'";
        case LEX_TOKEN_BLOCK_COMMENT:
            return "'/* ... */";
        case LEX_TOKEN_OBRACE:
            return "'{'";
        case LEX_TOKEN_EBRACE:
            return "'}'";
        case LEX_TOKEN_COLON:
            return "':'";
        case LEX_TOKEN_SEMICOLON:
            return "';'";
        case LEX_TOKEN_ID:
            return "'identifier'";
        case LEX_TOKEN_NUM:
            return "'number'";
        case LEX_TOKEN_NONE:
        default:
            return "";
    }
}

// Lexer entry points
_confToken_t* _confLex()
{
    // Just lex and return
    return _lexInternal();
}
