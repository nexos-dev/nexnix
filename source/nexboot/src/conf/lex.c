/*
    lex.c - contains shell conf lexer
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

#include "conf.h"
#include <assert.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

// Valid error states for lexer
#define LEX_ERROR_NONE            0
#define LEX_ERROR_UNEXPECTED_EOF  3
#define LEX_ERROR_BUFFER_OVERFLOW 6
#define LEX_ERROR_INTERNAL        8

#define LEX_BUF_SZ  512
#define LEX_STR_MAX 256

// Helper function macros
#define CHECK_NEWLINE_BREAK            \
    if (c == '\n')                     \
    {                                  \
        ++lex->line;                   \
        tok->type = LEX_TOKEN_NONE;    \
        lexReturnChar (lex, c);        \
        break;                         \
    }                                  \
    else if (c == '\r')                \
    {                                  \
        if (lexPeekChar (ctx) == '\n') \
            lexSkipChar (lex);         \
        ++lex->line;                   \
        tok->type = LEX_TOKEN_NONE;    \
        break;                         \
    }

#define CHECK_NEWLINE                  \
    if (c == '\n')                     \
    {                                  \
        ++lex->line;                   \
        tok->type = LEX_TOKEN_NONE;    \
    }                                  \
    else if (c == '\r')                \
    {                                  \
        if (lexPeekChar (ctx) == '\n') \
            lexSkipChar (lex);         \
        ++lex->line;                   \
        tok->type = LEX_TOKEN_NONE;    \
    }

#define CHECK_EOF(c)            \
    if ((c) == '\0')            \
    {                           \
        lex->isAccepted = true; \
        if (lex->isEof)         \
            goto end;           \
        else                    \
            goto intError;      \
    }

#define CHECK_EOF_BREAK(c)      \
    if ((c) == '\0')            \
    {                           \
        lex->isAccepted = true; \
        if (lex->isEof)         \
            break;              \
        else                    \
            goto intError;      \
    }

#define EXPECT_NO_EOF(c)                                    \
    if ((c) == '\0')                                        \
    {                                                       \
        if (lex->isEof)                                     \
            lexError (lex, LEX_ERROR_UNEXPECTED_EOF, NULL); \
        else                                                \
            goto intError;                                  \
        goto error;                                         \
    }

// Prints out an error condition
static inline void lexError (confLexState_t* state, int err, const char* extra)
{
#define ERRORBUFSZ 512
    char bufData[ERRORBUFSZ] = {0};

    char* obuf = bufData;
    char* buf = bufData;
    buf += snprintf (buf,
                     ERRORBUFSZ - (buf - obuf),
                     "nexboot: error: line %d: ",
                     state->line);
    // Decide how to handle the error
    switch (err)
    {
        case LEX_ERROR_UNEXPECTED_EOF:
            // Print out string
            buf += snprintf (buf, ERRORBUFSZ - (buf - obuf), "Unexpected EOF");
            if (state->tok->type)
            {
                buf += snprintf (buf,
                                 ERRORBUFSZ - (buf - obuf),
                                 " on token %s",
                                 confGetTokName (state->tok));
            }
            break;
        case LEX_ERROR_BUFFER_OVERFLOW:
            buf += snprintf (buf,
                             ERRORBUFSZ - (buf - obuf),
                             "Name too long on token %s",
                             confGetTokName (state->tok));
            break;
        case LEX_ERROR_INTERNAL:
            buf += snprintf (buf, ERRORBUFSZ - (buf - obuf), "Internal error");
            break;
    }
    NbLogMessage (obuf, NEXBOOT_LOGLEVEL_ERROR);
    NbLogMessage ("\n", NEXBOOT_LOGLEVEL_ERROR);
}

// Reads from file
char lexReadFile (ConfContext_t* ctx)
{
    confLexState_t* lex = &ctx->lexer;
    // Check if we need to read a buffer
    if (lex->bufPos == lex->bufSz)
    {
        // Read it in
        int32_t bytesRead = NbVfsReadFile (ctx->confFile->fileSys,
                                           ctx->confFile,
                                           StrRefGet (lex->buf),
                                           LEX_BUF_SZ);
        // Check for error
        if (bytesRead == -1)
        {
            lexError (lex, LEX_ERROR_INTERNAL, NULL);
            return INT8_MAX;
        }
        // Check EOF
        else if (!bytesRead)
            return 0;
        lex->bufSz = bytesRead;
        lex->bufPos = 0;
    }
    return 1;
}

// Reads a char from buffer
static inline char lexReadChar (ConfContext_t* ctx)
{
    char c = 0;
    confLexState_t* lex = &ctx->lexer;
    if (lex->nextChar)
    {
        c = lex->nextChar;
        // Reset it
        lex->nextChar = 0;
    }
    else
    {
        if (ctx->isFile)
        {
            // Read in next byte from file
            char res = lexReadFile (ctx);
            if (!res)
            {
                lex->isEof = true;
                return 0;
            }
            else if (res == INT8_MAX)
                return 0;
        }
        else
        {
            // Check EOF
            if (lex->bufPos == lex->bufSz)
            {
                lex->isEof = true;
                return 0;
            }
        }
        char* buf = StrRefGet (lex->buf);
        c = buf[lex->bufPos];
        ++lex->bufPos;
    }
    return c;
}

// Peeks at a character
static inline char lexPeekChar (ConfContext_t* ctx)
{
    char c = 0;
    confLexState_t* lex = &ctx->lexer;
    if (lex->nextChar)
        c = lex->nextChar;
    else
    {
        if (ctx->isFile)
        {
            // Read in next byte from file
            char res = lexReadFile (ctx);
            if (!res)
            {
                lex->isEof = true;
                return 0;
            }
            else if (res == INT8_MAX)
                return 0;
        }
        else
        {
            // Check EOF
            if (lex->bufPos == lex->bufSz)
            {
                lex->isEof = true;
                return 0;
            }
        }
        char* buf = StrRefGet (lex->buf);
        c = buf[lex->bufPos];
        ++lex->bufPos;
        lex->nextChar = c;
    }
    return c;
}

// Returns a character to the buffer
#define lexReturnChar(lex, c) ((lex)->nextChar = (c));

// Skips over a character that was peeked at
#define lexSkipChar(lex) ((lex)->nextChar = 0)

// Reads another line from shell
void lexReadNewLine (ConfContext_t* ctx)
{
    confLexState_t* lex = &ctx->lexer;
    // Reset state
    lex->bufPos = 0;
    lex->nextChar = 0;
    lex->curChar = 0;
    lex->line = 1;
    ctx->readCallback (ctx);
    lex->bufSz = ctx->bufSz;
}

// Checks if the current character is whitespace
static inline bool lexIsSpace (char c)
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

// Checks if the current character is a valid ID character
static inline bool lexIsIdChar (char c)
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
            return true;
    }
    return false;
}

// Checks if character is a token
static inline bool lexIsToken (char c)
{
    switch (c)
    {
        case '{':
        case '}':
        case '$':
        case '"':
        case '\'':
            return true;
    }
    return false;
}

// Lexes a token
confToken_t* confLex (ConfContext_t* ctx)
{
    confLexState_t* lex = &ctx->lexer;
    // Initialize token
    confToken_t* tok = calloc (sizeof (confToken_t), 1);
    if (!tok)
        goto error;
    lex->tok = tok;
    tok->type = LEX_TOKEN_NONE;
    // Check if we've finished lexing
    if (lex->isEof)
    {
        tok->type = LEX_TOKEN_EOF;
        tok->line = lex->line;
        return tok;
    }
    // Reset accepted state
    lex->isAccepted = false;
    while (!lex->isAccepted)
    {
        char c = lexReadChar (ctx);
        switch (c)
        {
            case '\0':
                // It's EOF, accept and return
                tok->type = LEX_TOKEN_EOF;
                tok->line = lex->line;
                lex->isAccepted = true;
                break;
            case ' ':
            case '\v':
            case '\f':
            case '\t':
                break;
            case '\r':
                // Carriage return. Can be Mac style (CR alone) or DOS style (CR
                // followed by LF) Look for an LF in case this is DOS style
                if (lexPeekChar (ctx) == '\n')
                    lexSkipChar (lex);
            // fall through
            case '\n':
                // Line feed. Increment current line
                ++lex->line;
                tok->type = LEX_TOKEN_NEWLINE;
                tok->line = lex->line;
                lex->isAccepted = true;
                break;
            case '#':
                // Comment. Just loop through every character until EOF or newline
            lexComment:
                c = lexReadChar (ctx);
                CHECK_EOF (c);
                CHECK_NEWLINE_BREAK (c);
                goto lexComment;
            // Single-character tokens
            case '{':
                tok->type = LEX_TOKEN_OBRACE;
                tok->line = lex->line;
                lex->isAccepted = true;
                break;
            case '}':
                tok->type = LEX_TOKEN_EBRACE;
                tok->line = lex->line;
                lex->isAccepted = true;
                break;
            case '$':
                tok->type = LEX_TOKEN_DOLLAR;
                tok->line = lex->line;
                lex->isAccepted = true;
                break;
            case '"':
            case '\'': {
                tok->type = LEX_TOKEN_LITERAL;
                tok->line = lex->line;
                // This is a string. Save what the terminator is
                char term = c;
                // Move to next character
                c = lexReadChar (ctx);
                EXPECT_NO_EOF (c);
                // Parse into semVal
                char* semVal = malloc (LEX_STR_MAX);
                if (!semVal)
                {
                    lexError (lex, LEX_ERROR_INTERNAL, NULL);
                    goto error;
                }
                int i = 0;
                // Loop until we hit a terminator
                while (c != term)
                {
                    // If character is a backlash
                    if (c == '\\')
                    {
                        // Figure out what was intended
                        if (lexPeekChar (ctx) == '\\')
                        {
                            lexSkipChar (lex);
                            c = '\\';
                        }
                        // Escape whitespace
                        else if (lexIsSpace (lexPeekChar (ctx)))
                        {
                            if (lexPeekChar (ctx) == '\n' ||
                                lexPeekChar (ctx) == '\r')
                            {
                                ++lex->line;
                                char oc = lexPeekChar (ctx);
                                lexSkipChar (lex);
                                // Skip over LF in case of CR
                                if (lexPeekChar (ctx) == '\n' && oc == '\r')
                                    lexSkipChar (lex);
                                c = lexReadChar (ctx);
                                EXPECT_NO_EOF (c);
                            }
                            else
                            {
                                lexSkipChar (lex);
                                c = lexReadChar (ctx);
                                EXPECT_NO_EOF (c);
                            }
                            // Continue skipping over whitespace
                            while (lexIsSpace (c))
                            {
                                c = lexReadChar (ctx);
                                EXPECT_NO_EOF (c);
                            }
                        }
                        else if (lexPeekChar (ctx) == term)
                        {
                            // Place in semVal and skip
                            lexSkipChar (lex);
                            c = term;
                        }
                    }
                    semVal[i] = c;
                    ++i;
                    if (i >= LEX_STR_MAX)
                    {
                        // Error
                        lexError (lex, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        free (semVal);
                        goto error;
                    }
                    c = lexReadChar (ctx);
                    EXPECT_NO_EOF (c)
                }
                // Null terminate
                semVal[i] = 0;
                // Prepare token
                tok->semVal = StrRefCreate (semVal);
                // Accept state
                lex->isAccepted = true;
                break;
            }
            default: {
                tok->line = lex->line;
                // This is probably an ID or unenclosed string.
                // Just copy into semVal until we know for sure
                char* semVal = calloc (LEX_STR_MAX, 1);
                if (!semVal)
                {
                    lexError (lex, LEX_ERROR_INTERNAL, NULL);
                    goto error;
                }
                // Keep track of non-ID characters
                bool isId = true;
                int i = 0;
                // Loop until we hit a space character, EOF, or token character
                while (!lexIsSpace (c) && !lexIsToken (c))
                {
                    // If character is a backlash
                    if (c == '\\')
                    {
                        // Figure out what was intended
                        if (lexPeekChar (ctx) == '\\')
                        {
                            lexSkipChar (lex);
                            c = '\\';
                        }
                        // Escape whitespace
                        else if (lexIsSpace (lexPeekChar (ctx)))
                        {
                            if (lexPeekChar (ctx) == '\n' ||
                                lexPeekChar (ctx) == '\r')
                            {
                                ++lex->line;
                                char oc = lexPeekChar (ctx);
                                lexSkipChar (lex);
                                // Skip over LF in case of CR
                                if (lexPeekChar (ctx) == '\n' && oc == '\r')
                                    lexSkipChar (lex);
                                c = lexReadChar (ctx);
                                // If this is a file, expecting no EOF. If not, then
                                // read another line
                                if (ctx->isFile)
                                {
                                    EXPECT_NO_EOF (c);
                                }
                                else
                                {
                                    // Read another line
                                    lexReadNewLine (ctx);
                                    // Read first character from line
                                    c = lexReadChar (ctx);
                                }
                                break;
                            }
                            else
                            {
                                lexSkipChar (lex);
                                c = lexReadChar (ctx);
                                EXPECT_NO_EOF (c);
                            }
                            // Continue skipping over whitespace
                            while (lexIsSpace (c))
                            {
                                c = lexReadChar (ctx);
                                EXPECT_NO_EOF (c);
                            }
                        }
                        // Escape token characters
                        else if (lexIsToken (lexPeekChar (ctx)))
                        {
                            c = lexPeekChar (ctx);
                            lexSkipChar (lex);
                        }
                    }
                    semVal[i] = c;
                    ++i;
                    if (i >= LEX_STR_MAX)
                    {
                        // Error
                        lexError (lex, LEX_ERROR_BUFFER_OVERFLOW, NULL);
                        free (semVal);
                        goto error;
                    }
                    c = lexReadChar (ctx);
                    CHECK_EOF_BREAK (c)
                }
                // Null terminate
                semVal[i] = 0;
                // Return last character, as it is not part of ID / string
                lexReturnChar (lex, c);
                // Check if it's a keyword
                if (!strcmp (semVal, "set"))
                    tok->type = LEX_TOKEN_SET;
                else if (!strcmp (semVal, "menuentry"))
                    tok->type = LEX_TOKEN_MENUENTRY;
                else if (isId)
                    tok->type = LEX_TOKEN_ID;    // It's an ID
                else
                    tok->type = LEX_TOKEN_STR;    // It's a string
                tok->semVal = StrRefCreate (semVal);
                // Accept state
                lex->isAccepted = true;
                break;
            }
        }
    }
end:
    return tok;
intError:
    lexError (lex, LEX_ERROR_INTERNAL, NULL);
error:
    tok->type = LEX_TOKEN_ERROR;
    tok->line = lex->line;
    return tok;
}

// Gets name of tok
const char* confGetTokName (confToken_t* tok)
{
    switch (tok->type)
    {
        case LEX_TOKEN_COMMENT:
            return "'#'";
        case LEX_TOKEN_OBRACE:
            return "'{'";
        case LEX_TOKEN_EBRACE:
            return "'}'";
        case LEX_TOKEN_DOLLAR:
            return "'$'";
        case LEX_TOKEN_ID:
            return "'identifier'";
        case LEX_TOKEN_STR:
            return "'string'";
        case LEX_TOKEN_EOF:
            return "'EOF'";
        case LEX_TOKEN_NEWLINE:
            return "'newline'";
        case LEX_TOKEN_LITERAL:
            return "'literal'";
        case LEX_TOKEN_SET:
            return "'set'";
        case LEX_TOKEN_MENUENTRY:
            return "'menuentry'";
        default:
            return "";
    }
    return "";
}

// Initializes lexer
bool confLexInit (ConfContext_t* ctx)
{
    ctx->lexer.line = 1;
    if (ctx->isFile)
    {
        char* buf = malloc (LEX_BUF_SZ);
        if (!buf)
            return NULL;
        ctx->lexer.buf = StrRefCreate (buf);
        ctx->lexer.bufSz = LEX_BUF_SZ;
        ctx->lexer.bufPos = LEX_BUF_SZ;    // To force a read
    }
    else
    {
        ctx->lexer.buf = ctx->line;
        ctx->lexer.bufSz = ctx->bufSz;
    }
    return true;
}

// Destroys lexer
bool confLexDestroy (ConfContext_t* ctx)
{
    StrRefDestroy (ctx->line);
}
