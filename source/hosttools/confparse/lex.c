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

#include "internal.h"
#include <assert.h>
#include <chardet/chardet.h>
#include <errno.h>
#include <libnex/error.h>
#include <libnex/safemalloc.h>
#include <libnex/textstream.h>
#include <stdlib.h>

#define LEX_FRAME_SZ 2048    // Size of lexing staging buffer

// Valid states for lexer
#define LEX_STATE_ACCEPT 1

// The state of the lexer
typedef struct _lexState
{
    TextStream_t* stream;    // Text stream object
    char32_t* buf;           // The staging buffer. Holds the current frame
    size_t bufSize;          // Size of staging buffer
    int framePos;            // Current position in current frame
    int prevFramePos;        // Frame position at start of lexing. Used for peeking
    size_t oldFilePos;       // Old file pointer when we starting lexing. Used for peeking
    int state;               // Current state of lexer
    _confToken_t tok;        // Current token
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
    detect_obj_free (&obj);
    // Allocate staging buffer
    state.bufSize = LEX_FRAME_SZ / sizeof (char32_t);
    state.buf = malloc_s (state.bufSize * sizeof (char32_t));
    // Set positions
    state.framePos = 0;
    state.oldFilePos = 0;
    // Set state related info
    state.state = 0;
}

void _confLexDestroy()
{
    assert (state.stream);
    TextClose (state.stream);
    free (state.buf);
}

// Reads in a new buffer
static void _confLexReadBuf()
{
    assert ((state.framePos == state.bufSize) || (state.framePos == 0));
    state.framePos = 0;
    size_t charsRead = 0;
    short res = 0;
    // Read it in
    if ((res = TextRead (state.stream, state.buf, state.bufSize, &charsRead)) != TEXT_SUCCESS)
        error ("%s", TextError (res));
    state.bufSize = charsRead;
}

// Reads a character from the current buffer
static char32_t _confLexReadChar()
{
    assert (state.stream);
    if (state.framePos == state.bufSize || state.framePos == 0)
    {
        // Read a new buffer
        _confLexReadBuf();
        // Check if we reached the end
        if (state.bufSize == 0)
            return 0;
    }
    // Read a character
    char32_t c = state.buf[state.framePos];
    ++state.framePos;
    return c;
}

_confToken_t* _confLex()
{
    char32_t curChar = _confLexReadChar();
    while (state.state != LEX_STATE_ACCEPT)
    {
        printf ("%lc\n", curChar);
        curChar = _confLexReadChar();
        if (curChar == 0)
            return NULL;
    }
    return &state.tok;
}
