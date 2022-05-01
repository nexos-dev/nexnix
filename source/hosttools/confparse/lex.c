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
#include <libnex/base.h>
#include <libnex/error.h>
#include <libnex/object.h>
#include <libnex/progname.h>
#include <libnex/safemalloc.h>
#include <libnex/textstream.h>
#include <limits.h>
#include <stdlib.h>

#define LEX_FRAME_SZ 2048    // Size of lexing staging buffer

#define VARNAME_SIZE 512

// Valid error states for lexer
#define LEX_ERROR_NONE             0
#define LEX_ERROR_UNEXPECTED_TOKEN 1
#define LEX_ERROR_UNKNOWN_TOKEN    2
#define LEX_ERROR_UNEXPECTED_EOF   3
#define LEX_ERROR_INTERNAL         4
#define LEX_ERROR_INVALID_NUMBER   5
#define LEX_ERROR_BUFFER_OVERFLOW  6
#define LEX_ERROR_INVALID_VAR_ID   7

// Helper function macros
#define CHECK_NEWLINE_BREAK               \
    if (curChar == '\n')                  \
    {                                     \
        ++state->line;                    \
        tok->type = LEX_TOKEN_NONE;       \
        break;                            \
    }                                     \
    else if (curChar == '\r')             \
    {                                     \
        if (_lexPeekChar (state) == '\n') \
            _lexSkipChar (state);         \
        ++state->line;                    \
        tok->type = LEX_TOKEN_NONE;       \
        break;                            \
    }

#define CHECK_NEWLINE                     \
    if (curChar == '\n')                  \
    {                                     \
        ++state->line;                    \
        tok->type = LEX_TOKEN_NONE;       \
    }                                     \
    else if (curChar == '\r')             \
    {                                     \
        if (_lexPeekChar (state) == '\n') \
            _lexSkipChar (state);         \
        ++state->line;                    \
        tok->type = LEX_TOKEN_NONE;       \
    }

#define CHECK_EOF(c)              \
    if ((c) == '\0')              \
    {                             \
        state->isAccepted = true; \
        goto end;                 \
    }

#define CHECK_EOF_BREAK(c)        \
    if ((c) == '\0')              \
    {                             \
        state->isAccepted = true; \
        break;                    \
    }

#define EXPECT_NO_EOF(c)                                   \
    if ((c) == '\0')                                       \
    {                                                      \
        _lexError (state, LEX_ERROR_UNEXPECTED_EOF, NULL); \
        goto _internalError;                               \
    }

// Prints out an error condition
static inline void _lexError (lexState_t* state, int err, const char* extra)
{
    mbstate_t mbState;
    memset (&mbState, 0, sizeof (mbstate_t));
    size_t mbBytesWritten = 0;

    char extraBuf[5];
    char bufData[2048];

    char* obuf = bufData;
    char* buf = bufData;

    if (err != LEX_ERROR_INTERNAL)
        buf += snprintf (buf, 2048 - (buf - obuf), "error: %s:", ConfGetFileName());

    if (state)
        buf += snprintf (buf, 2048 - (buf - obuf), "%d: ", state->line);
    else
        buf += snprintf (buf, 2048 - (buf - obuf), " ");
    // Decide how to handle the error
    switch (err)
    {
        case LEX_ERROR_UNEXPECTED_TOKEN:
            // Print out error string
            buf += snprintf (buf, 2048 - (buf - obuf), "Unexpected token ");
            buf += snprintf (buf, 2048 - (buf - obuf), "%s", _confLexGetTokenName (state->tok));
            break;
        case LEX_ERROR_UNKNOWN_TOKEN:
            // Convert current char to a char
            mbBytesWritten = c32rtomb (extraBuf, state->curChar, &mbState);
            if (mbBytesWritten == -1)
                error ("internal error: %s", strerror (errno));
            else
            {
                extraBuf[mbBytesWritten] = '\0';
                // Add everything
                buf += snprintf (buf, 2048 - (buf - obuf), "Unknown token '%s'", extraBuf);
            }
            break;
        case LEX_ERROR_UNEXPECTED_EOF:
            // Print out string
            buf += snprintf (buf, 2048 - (buf - obuf), "Unexpected EOF");
            buf += snprintf (buf, 2048 - (buf - obuf), " on token %s", _confLexGetTokenName (state->tok));
            break;
        case LEX_ERROR_INVALID_NUMBER:
            buf += snprintf (buf, 2048 - (buf - obuf), "Invalid numeric value");
            break;
        case LEX_ERROR_BUFFER_OVERFLOW:
            buf += snprintf (buf,
                             2048 - (buf - obuf),
                             "Name too long on token %s",
                             _confLexGetTokenName (state->tok));
            break;
        case LEX_ERROR_INVALID_VAR_ID:
            buf += snprintf (buf, 2048 - (buf - obuf), "Invalid character in variable");
            break;
        case LEX_ERROR_INTERNAL:
            buf += snprintf (buf, 2048 - (buf - obuf), "internal error: %s", extra);
            break;
    }
    // Silence clang-tidy warnings about buf being unused
    (void) buf;
    error (obuf);
}

lexState_t* _confLexInit (const char* file)
{
    assert (file);
    // Create state
    lexState_t* state = (lexState_t*) calloc_s (sizeof (lexState_t));
    if (!state)
        return NULL;
    // Detect character set
    DetectObj* obj = detect_obj_init();
    short res = 0;
    if ((res = detect_file (file, 8192, &obj)) != CHARDET_SUCCESS)
    {
        if (res == CHARDET_IO_ERROR)
        {
            free (state);
            detect_obj_free (&obj);
            _lexError (NULL, LEX_ERROR_INTERNAL, strerror (errno));
            return NULL;
        }
        else
        {
            free (state);
            detect_obj_free (&obj);
            _lexError (NULL, LEX_ERROR_INTERNAL, "unable to detect character set");
            return NULL;
        }
    }
    char enc = 0, order = 0;
    TextGetEncId (obj->encoding, &enc, &order);
    // Open up the text stream
    res = TextOpen (file, &state->stream, TEXT_MODE_READ, enc, obj->bom, order);
    if (res != TEXT_SUCCESS)
    {
        free (state);
        detect_obj_free (&obj);
        _lexError (NULL, LEX_ERROR_INTERNAL, TextError (res));
        return NULL;
    }
    // Set up state
    state->line = 1;
    // Free stuff we're done with
    detect_obj_free (&obj);
    return state;
}

void _confLexDestroy (lexState_t* state)
{
    if (state->stream)
        TextClose (state->stream);
    free (state);
}

// Reads a character from the file
static inline char32_t _lexReadChar (lexState_t* state)
{
    char32_t c = 0;
    short res = 0;
    // Check if state.nextChar is set
    if (state->nextChar)
    {
        c = state->nextChar;
        // Reset it so we know to advance
        state->nextChar = 0;
    }
    else
    {
        // Read in the character
        TextReadChar (state->stream, &c);
        // Check for end of file
        if (TextIsEof (state->stream))
        {
            state->isEof = 1;
            return '\0';
        }
    }
    state->curChar = c;
    return c;
}

// Peek at the next character in the file
static inline char32_t _lexPeekChar (lexState_t* state)
{
    char32_t c = 0;
    short res = 0;
    // Check if nextChar is set
    if (state->nextChar)
        c = state->nextChar;
    else
    {
        // Read in a character, setting nextChar
        TextReadChar (state->stream, &c);
        // Check for end of file
        if (TextIsEof (state->stream))
        {
            state->isEof = 1;
            return '\0';
        }
        state->nextChar = c;
    }
    return c;
}

// Returns a character to the buffer
#define _lexReturnChar(state, c) ((state)->nextChar = (c));

// Skips over a character that was peeked at
#define _lexSkipChar(state) ((state)->nextChar = 0)

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
static inline bool _lexIsNumeric (char32_t c, uint8_t base)
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
        case 'A':
        case 'a':
        case 'B':
        case 'b':
        case 'C':
        case 'c':
        case 'D':
        case 'd':
        case 'E':
        case 'e':
        case 'F':
        case 'f':
            return base == 16;
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
_confToken_t* _lexInternal (lexState_t* state)
{
    assert (state->stream);
    unsigned long bufPos = 0;
    int numBufPos = 0;
    int res = 0;
    // Allocate token
    _confToken_t* tok = (_confToken_t*) malloc_s (sizeof (_confToken_t));
    if (!tok)
        goto _internalError;
    state->tok = tok;
    tok->type = LEX_TOKEN_NONE;
    // If we're at the end of the file, report it
    if (state->isEof)
    {
        tok->line = state->line;
        tok->type = LEX_TOKEN_EOF;
        return tok;
    }
    // Ensure that state is not currently acceptec
    state->isAccepted = false;
    while (!state->isAccepted)
    {
        // Read in a character
        char32_t curChar = _lexReadChar (state);
        // Decide what to do with this character
        switch (curChar)
        {
            case '\0':
                // Unconditionally accept on EOF
                state->isAccepted = true;
                break;
            case ' ':
            case '\v':
            case '\f':
            case '\t':
                break;
            case '\r':
                // Carriage return. Can be Mac style (CR alone) or DOS style (CR followed by LF)
                // Look for an LF in case this is DOS style
                if (_lexPeekChar (state) == '\n')
                    _lexSkipChar (state);
            // fall through
            case '\n':
                // Line feed. Increment current line
                ++state->line;
                break;
            case '#':
                // Comment starting with a pound
                tok->type = LEX_TOKEN_POUND_COMMENT;
                goto lexComment;
            case '/':
                // Check for a single line comment
                if (_lexPeekChar (state) == '/')
                {
                    _lexSkipChar (state);
                    tok->type = LEX_TOKEN_SLASH_COMMENT;
                    goto lexComment;
                }
                // Maybe a block comment?
                else if (_lexPeekChar (state) == '*')
                {
                    _lexSkipChar (state);
                    tok->type = LEX_TOKEN_BLOCK_COMMENT;
                // Lex it
                lexBlockComment:
                    curChar = _lexReadChar (state);
                    if (curChar == '*')
                    {
                        // Look for a '/'
                        if (_lexPeekChar (state) == '/')
                        {
                            // End of comment
                            _lexSkipChar (state);
                            break;
                        }
                    }
                    else if (curChar == '\r')
                    {
                        // Still increment line
                        if (_lexPeekChar (state) == '\n')
                            _lexSkipChar (state);
                        ++state->line;
                    }
                    else if (curChar == '\n')
                        ++state->line;

                    EXPECT_NO_EOF (curChar);
                    goto lexBlockComment;
                }
                else
                    goto unkownToken;
            // Fall through
            lexComment:
                // This is a comment
                // Iterate through the comment
                curChar = _lexReadChar (state);
                CHECK_EOF (curChar);
                // Check for a newline
                CHECK_NEWLINE_BREAK
                goto lexComment;
            // Lex single character identifiers
            case '{':
                // Prepare token
                tok->type = LEX_TOKEN_OBRACE;
                tok->line = state->line;
                // Accept it
                state->isAccepted = true;
                break;
            case '}':
                // Prepare it
                tok->type = LEX_TOKEN_EBRACE;
                tok->line = state->line;
                // Accept
                state->isAccepted = true;
                break;
            case ':':
                // Prepare it
                tok->type = LEX_TOKEN_COLON;
                tok->line = state->line;
                // Accept
                state->isAccepted = true;
                break;
            case ';':
                // Prepare and accept
                tok->type = LEX_TOKEN_SEMICOLON;
                tok->line = state->line;
                state->isAccepted = true;
                break;
            case ',':
                // Same thing
                tok->type = LEX_TOKEN_COMMA;
                tok->line = state->line;
                state->isAccepted = true;
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
            lexId:
                // Prepare it
                tok->type = LEX_TOKEN_ID;
                tok->line = state->line;
                // Add the rest of it
                while (_lexIsIdChar (curChar))
                {
                    if (bufPos >= TOK_SEM_SIZE)
                    {
                        _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        goto _internalError;
                    }
                    tok->semVal[bufPos] = (char) curChar;
                    ++bufPos;
                    curChar = _lexReadChar (state);
                    CHECK_EOF_BREAK (curChar);
                }
                // Null terminate it
                tok->semVal[bufPos] = 0;
                // Return character to buffer
                _lexReturnChar (state, curChar);
                // Check if this is a keyword
                if (!strcmp (tok->semVal, "include"))
                    tok->type = LEX_TOKEN_INCLUDE;
                // Accept
                state->isAccepted = true;
                break;
            case '0':
                // Figure out base when a number starts with 0
                if (_lexPeekChar (state) == 'x')
                {
                    // Skip and set base
                    _lexSkipChar (state);
                    tok->base = 16;
                }
                else if (_lexPeekChar (state) == 'b')
                {
                    // Same thing
                    _lexSkipChar (state);
                    tok->base = 2;
                }
                else
                    tok->base = 8;
                curChar = _lexReadChar (state);
                CHECK_EOF (curChar);
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
                tok->line = state->line;
                // Add rest of value
                while (_lexIsNumeric (curChar, tok->base) || (bufPos == 0 && curChar == '-'))
                {
                    if (bufPos >= TOK_SEM_SIZE)
                    {
                        _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        goto _internalError;
                    }
                    tok->semVal[bufPos] = (char) curChar;
                    ++bufPos;
                    curChar = _lexReadChar (state);
                    CHECK_EOF (curChar);
                }
                // Ensure the user didn't just pass '-'
                if (bufPos <= 1 && tok->semVal[0] == '-')
                {
                    _lexError (state, LEX_ERROR_INVALID_NUMBER, NULL);
                    goto _internalError;
                }
                // Check if the first invalid character really is a valid ID character
                if (_lexIsIdChar (curChar))
                {
                    // Re-lex as an ID
                    goto lexId;
                }
                // Null terminate
                tok->semVal[bufPos] = 0;
                // Return first non-numeric character
                _lexReturnChar (state, curChar);
                // Convert the string to numeric
                tok->num = strtoll (tok->semVal, NULL, tok->base);
                if (tok->num == LONG_MIN || tok->num == LONG_MAX)
                {
                    _lexError (state, LEX_ERROR_INTERNAL, strerror (errno));
                    goto _internalError;
                }
                // Accept it
                state->isAccepted = true;
                break;
            case '\'':
                // A literal string. Simply lex into semVal
                tok->type = LEX_TOKEN_STR;
                tok->line = state->line;
                curChar = _lexReadChar (state);
                while (curChar != '\'')
                {
                    // Handle escape sequences
                    if (curChar == '\\')
                    {
                        // Look at next character
                        if (_lexPeekChar (state) == '\\')
                        {
                            // Write out a single backslash
                            _lexSkipChar (state);
                            curChar = '\\';
                        }
                        // Escape whitespace
                        else if (_lexIsSpace (_lexPeekChar (state)))
                        {
                            if (_lexPeekChar (state) == '\n' || _lexPeekChar (state) == '\r')
                            {
                                ++state->line;
                                char32_t oc = _lexPeekChar (state);
                                _lexSkipChar (state);
                                // Skip over LF in case of CR
                                if (_lexPeekChar (state) == '\n' && oc == '\r')
                                    _lexSkipChar (state);
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                            else
                            {
                                _lexSkipChar (state);
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                            // Continue skipping over whitespace
                            while (_lexIsSpace (curChar))
                            {
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                        }
                        // Escape apostrophes
                        else if (_lexPeekChar (state) == '\'')
                        {
                            _lexSkipChar (state);
                            curChar = '\'';
                        }
                    }
                    if (bufPos >= TOK_SEM_SIZE)
                    {
                        _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        goto _internalError;
                    }
                    tok->strVal[bufPos] = curChar;
                    ++bufPos;
                    curChar = _lexReadChar (state);
                    EXPECT_NO_EOF (curChar);
                }
                tok->strVal[bufPos] = 0;
                state->isAccepted = true;
                break;
            case '"':
                // A string potentially with variable references and other escapes.
                // This is the hardest contsruct to lex
                tok->type = LEX_TOKEN_STR;
                tok->line = state->line;
                curChar = _lexReadChar (state);
                while (curChar != '"')
                {
                    // Handle escape sequences
                    if (curChar == '\\')
                    {
                        // Look at next character
                        if (_lexPeekChar (state) == '\\')
                        {
                            // Write out a single backslash
                            _lexSkipChar (state);
                            curChar = '\\';
                        }
                        // Escape whitespace
                        else if (_lexIsSpace (_lexPeekChar (state)))
                        {
                            if (_lexPeekChar (state) == '\n' || _lexPeekChar (state) == '\r')
                            {
                                ++state->line;
                                char32_t oc = _lexPeekChar (state);
                                _lexSkipChar (state);
                                // Skip over LF in case of CR
                                if (_lexPeekChar (state) == '\n' && oc == '\r')
                                    _lexSkipChar (state);
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                            else
                            {
                                _lexSkipChar (state);
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                            // Continue skipping over whitespace
                            while (_lexIsSpace (curChar))
                            {
                                curChar = _lexReadChar (state);
                                EXPECT_NO_EOF (curChar);
                            }
                            continue;
                        }
                        // Escape quotation marks
                        else if (_lexPeekChar (state) == '"')
                        {
                            // Write it out
                            _lexSkipChar (state);
                            curChar = '"';
                        }
                        // Escape dollar signs
                        else if (_lexPeekChar (state) == '$')
                        {
                            _lexSkipChar (state);
                            curChar = '$';
                        }
                        // Escape newlines
                        else if (_lexPeekChar (state) == 'n')
                        {
                            _lexSkipChar (state);
                            curChar = '\n';
                        }
                    }
                    // Variable reference
                    else if (curChar == '$')
                    {
                        // Get variable name
                        char varName[VARNAME_SIZE] = {0};
                        unsigned long varBufPos = 0;
                        curChar = _lexReadChar (state);
                        while (curChar != '$')
                        {
                            // Check validity
                            if (!_lexIsIdChar (curChar))
                            {
                                _lexError (state, LEX_ERROR_INVALID_VAR_ID, NULL);
                                goto _internalError;
                            }
                            // Add to buffer
                            varName[varBufPos] = (char) curChar;
                            ++varBufPos;
                            if (varBufPos >= VARNAME_SIZE)
                            {
                                _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                                goto _internalError;
                            }
                            // Get next char
                            curChar = _lexReadChar (state);
                        }
                        // Get variable contents
                        char* var = getenv (varName);
                        if (var)
                        {
                            // Convert to char32_t
                            size_t varLen = strlen (var) * sizeof (char32_t);
                            char32_t* var32 = (char32_t*) malloc_s (varLen);
                            mbstate_t mbstate;
                            memset (&mbstate, 0, sizeof (mbstate_t));
                            if (mbstoc32s (var32, var, varLen, varLen / sizeof (char32_t), &mbstate) == -1)
                            {
                                free (var32);
                                _lexError (state, LEX_ERROR_INTERNAL, strerror (errno));
                                goto _internalError;
                            }
                            tok->strVal[bufPos] = 0;
                            // Concatenate variable name
                            if (c32lcat (tok->strVal, var32, TOK_SEM_SIZE * sizeof (char32_t)) >=
                                (TOK_SEM_SIZE * sizeof (char32_t)))
                            {
                                free (var32);
                                _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                                goto _internalError;
                            }
                            bufPos += (varLen / sizeof (char32_t));
                            free (var32);
                        }
                        goto strEnd;
                    }
                    if (bufPos >= TOK_SEM_SIZE)
                    {
                        _lexError (state, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        goto _internalError;
                    }
                    tok->strVal[bufPos] = curChar;
                    ++bufPos;
                strEnd:
                    curChar = _lexReadChar (state);
                    EXPECT_NO_EOF (curChar);
                }
                tok->strVal[bufPos] = 0;
                state->isAccepted = true;
                break;
            unkownToken:
            default:
                // An error here
                _lexError (state, LEX_ERROR_UNKNOWN_TOKEN, NULL);
                goto _internalError;
        }
    }
end:
    return state->tok;
_internalError:
    state->tok->type = LEX_TOKEN_ERROR;
    return state->tok;
}

const char* _confLexGetTokenName (_confToken_t* tok)
{
    return _confLexGetTokenNameType (tok->type);
}

const char* _confLexGetTokenNameType (int type)
{
    switch (type)
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
        case LEX_TOKEN_COMMA:
            return "','";
        case LEX_TOKEN_ID:
            return "'identifier'";
        case LEX_TOKEN_NUM:
            return "'number'";
        case LEX_TOKEN_STR:
            return "'string'";
        case LEX_TOKEN_EOF:
            return "'EOF'";
        default:
            return "";
    }
}

// Lexer entry points
_confToken_t* _confLex (lexState_t* state)
{
    // Just lex and return
    return _lexInternal (state);
}
