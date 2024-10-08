/*
    vprintf_core.c - contains printf core functions
    Copyright 2023 - 2024 The NexNix Project

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

#include "printf_family.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define __NEED_PTRDIFFT
#include <bits/types.h>

// Flags
#define PRINTF_FLAG_LEFT_JUSTIFY (1 << 0)
#define PRINTF_FLAG_ALWAYS_SIGN  (1 << 1)
#define PRINTF_FLAG_SPACE_SIGN   (1 << 2)
#define PRINTF_FLAG_PREFIX       (1 << 3)
#define PRINTF_FLAG_0PAD         (1 << 4)

// Length modifiers
#define PRINTF_LEN_CHAR      1
#define PRINTF_LEN_SHORT     2
#define PRINTF_LEN_LONG      3
#define PRINTF_LEN_LONG_LONG 4
#define PRINTF_LEN_INTMAX    5
#define PRINTF_LEN_SIZET     6
#define PRINTF_LEN_PTRDIFF   7

// Conversion specifiers
#define PRINTF_CONV_DECIMAL       0
#define PRINTF_CONV_OCTAL         1
#define PRINTF_CONV_UNSIGNED      2
#define PRINTF_CONV_HEX_LOWER     3
#define PRINTF_CONV_HEX_UPPER     4
#define PRINTF_CONV_CHAR          5
#define PRINTF_CONV_STRING        6
#define PRINTF_CONV_PTR           7
#define PRINTF_CONV_WRITTEN_CHARS 8

// Size specifiers
#define PRINTF_SIZE_SCHAR     1
#define PRINTF_SIZE_UCHAR     2
#define PRINTF_SIZE_SSHORT    3
#define PRINTF_SIZE_USHORT    4
#define PRINTF_SIZE_SINT      5
#define PRINTF_SIZE_UINT      6
#define PRINTF_SIZE_SLONG     7
#define PRINTF_SIZE_ULONG     8
#define PRINTF_SIZE_SLONGLONG 9
#define PRINTF_SIZE_ULONGLONG 10
#define PRINTF_SIZE_PCHAR     11
#define PRINTF_SIZE_STRING    12
#define PRINTF_SIZE_INTMAX    13
#define PRINTF_SIZE_UINTMAX   14
#define PRINTF_SIZE_SIZET     15
#define PRINTF_SIZE_PTRDIFF   16
#define PRINTF_SIZE_UINTPTR   17

// Length to size tables
static int lenToSizeInt[] = {PRINTF_SIZE_SINT,
                             PRINTF_SIZE_SCHAR,
                             PRINTF_SIZE_SSHORT,
                             PRINTF_SIZE_SLONG,
                             PRINTF_SIZE_SLONGLONG,
                             PRINTF_SIZE_INTMAX,
                             PRINTF_SIZE_SIZET,
                             PRINTF_SIZE_PTRDIFF};

static int lenToSizeUint[] = {PRINTF_SIZE_UINT,
                              PRINTF_SIZE_UCHAR,
                              PRINTF_SIZE_USHORT,
                              PRINTF_SIZE_ULONG,
                              PRINTF_SIZE_ULONGLONG,
                              PRINTF_SIZE_UINTMAX,
                              PRINTF_SIZE_SIZET,
                              PRINTF_SIZE_PTRDIFF};

#define IS_DIGIT1TO9(c)                                                                  \
    ((c) == '1' || (c) == '2' || (c) == '3' || (c) == '4' || (c) == '5' || (c) == '6' || \
     (c) == '7' || (c) == '8' || (c) == '9')
#define INC_FORMAT \
    ++fmt;         \
    ++fmtOffset;

static int __fmtStrToNum (const char** fmts, int* fmtOffset)
{
    int i = 0;
    int num = 0;
    const char* fmt = *fmts;
    while (IS_DIGIT1TO9 (*fmt))
    {
        // Convert to number
        if (num)
            num *= 10;    // Move to next base
        num += (*fmt - '0');
        ++i;
        ++fmt;
        *fmtOffset += 1;
    }
    *fmts = fmt;
    return num;
}

static bool __fmtSignedNumToStr (char* s, intmax_t num)
{
    // Determine sign
    bool sign = 0;
    if (num >= 0)
        sign = 1;
    else
    {
        sign = 0;
        num *= -1;    // Change to positive to make it easier to work with
    }
    char buf[25];
    int bufPos = 0;
    // Begin converting
    do
    {
        buf[bufPos] = ((char) (num % 10)) + '0';
        num /= 10;
        ++bufPos;
    } while (num);
    // So, we have a string, it's just backwards. Let's fix that
    for (int i = 0, j = bufPos; j != 0; j--, i++)
        s[i] = buf[j - 1];
    return sign;
}

static void __fmtUnsignedNumToStr (char* s, uintmax_t num, int base, bool upperCase)
{
    char obuf[25];
    char* buf = obuf;
    int bufPos = 0;
    // Begin converting
    do
    {
        char curNum = ((char) (num % base));
        if (curNum >= 10)
        {
            // Convert to a letter
            if (upperCase)
                curNum += ('A' - 10);
            else
                curNum += ('a' - 10);
        }
        else
            curNum += '0';
        buf[bufPos] = curNum;
        num /= base;
        ++bufPos;
    } while (num);
    // So, we have a string, it's just backwards. Let's fix that
    for (int i = 0, j = bufPos; j != 0; j--, i++)
        s[i] = buf[j - 1];
}

static int __outString (_printfOut_t* out, const char* s, size_t charsToPrint)
{
    while (*s && charsToPrint)
    {
        if (out->out (out, *s) == EOF)
            return EOF;
        ++s;
        --charsToPrint;
    }
    return 0;
}

static int __printArg (_printfFmt_t* fmt, _printfOut_t* out)
{
    const char* s = NULL;
    char buf[64] = {0};
    bool accountPrecision = false;
    int precisionChars = 0;
    int widthChars = 0;
    int prefixChars = 0;
    char fieldWidthChar = ' ';
    bool sign = true;
    size_t charsToPrint = SIZE_MAX;
    // Figure out the conversion to do
    switch (fmt->conv)
    {
        case PRINTF_CONV_DECIMAL:
            //  Convert to signed value
            sign = __fmtSignedNumToStr (buf, fmt->sdata);
            s = buf;
            accountPrecision = true;
            if (fmt->sdata == 0 && fmt->precision == 0)
                return 0;    // Special case in spec
            // Add prefix characters
            if (fmt->sdata <= 0 ||
                ((fmt->flags & PRINTF_FLAG_ALWAYS_SIGN) == PRINTF_FLAG_ALWAYS_SIGN))
            {
                ++prefixChars;
            }
            goto numCommon;
        case PRINTF_CONV_UNSIGNED:
            __fmtUnsignedNumToStr (buf, fmt->udata, 10, false);
            s = buf;
            accountPrecision = true;
            goto numCommon;
        case PRINTF_CONV_HEX_LOWER:
            __fmtUnsignedNumToStr (buf, fmt->udata, 16, false);
            s = buf;
            accountPrecision = true;
            if ((fmt->flags & PRINTF_FLAG_PREFIX) == PRINTF_FLAG_PREFIX)
                prefixChars += 2;
            goto numCommon;
        case PRINTF_CONV_HEX_UPPER:
            __fmtUnsignedNumToStr (buf, fmt->udata, 16, true);
            s = buf;
            accountPrecision = true;
            if ((fmt->flags & PRINTF_FLAG_PREFIX) == PRINTF_FLAG_PREFIX)
                prefixChars += 2;
            goto numCommon;
        case PRINTF_CONV_OCTAL:
            __fmtUnsignedNumToStr (buf, fmt->udata, 8, false);
            s = buf;
            accountPrecision = true;
            if ((fmt->flags & PRINTF_FLAG_PREFIX) == PRINTF_FLAG_PREFIX)
                prefixChars++;
            goto numCommon;
        numCommon:
            if ((fmt->flags & PRINTF_FLAG_0PAD) == PRINTF_FLAG_0PAD)
                fieldWidthChar = '0';
            if (fmt->conv != PRINTF_CONV_DECIMAL && fmt->udata == 0 && fmt->precision == 0)
            {
                return 0;    // Special case
            }
            break;
        case PRINTF_CONV_PTR:
            __fmtUnsignedNumToStr (buf, fmt->ptr, 16, true);
            prefixChars = 2;
            fmt->flags |= PRINTF_FLAG_PREFIX;
            s = buf;
            break;
        case PRINTF_CONV_CHAR:
            out->out (out, (char) fmt->udata);
            return 0;
        case PRINTF_CONV_STRING:
            s = (const char*) fmt->ptr;
            accountPrecision = true;

            break;
        case PRINTF_CONV_WRITTEN_CHARS:
            break;
        default:
            return 0;
    }
    if (accountPrecision)
    {
        // On strings, precision is number of characters to print
        if (fmt->conv == PRINTF_CONV_STRING)
        {
            size_t strLen = strlen (s);
            charsToPrint = (strLen > fmt->precision) ? fmt->precision : strLen;
            if (fmt->precisionIsDefault)
            {
                fmt->precision = charsToPrint;
                charsToPrint = strLen;
            }
        }
        // Figure out current number of characters
        int curCharCount = (int) strlen (s);
        precisionChars = fmt->precision - curCharCount;
        if (precisionChars < 0)
            precisionChars = 0;    // If negative, treat as zero
    }
    // Account for field width
    if (fmt->width)
    {
        // Determine number of field with characters
        int curCharCount = (int) strlen (s);
        if (curCharCount > charsToPrint)
            curCharCount = charsToPrint;
        widthChars = fmt->width - (curCharCount + precisionChars);
        if (widthChars < 0)
            widthChars = 0;
    }
    // Write prefix (if any)
    if ((fmt->flags & PRINTF_FLAG_PREFIX) == PRINTF_FLAG_PREFIX)
    {
        switch (fmt->conv)
        {
            case PRINTF_CONV_HEX_LOWER:
            case PRINTF_CONV_HEX_UPPER:
            case PRINTF_CONV_PTR:
                if (__outString (out, "0x", SIZE_MAX) == EOF)
                    return EOF;
                break;
            case PRINTF_CONV_OCTAL:
                if (out->out (out, '0') == EOF)
                    return EOF;
                break;
        }
    }
    // If being right justified, go ahead and write field width characters
    if ((fmt->flags & PRINTF_FLAG_LEFT_JUSTIFY) != PRINTF_FLAG_LEFT_JUSTIFY)
    {
        for (int i = 0; i < widthChars; ++i)
        {
            if (out->out (out, fieldWidthChar) == EOF)
                return EOF;
        }
    }
    // Write sign
    if (fmt->conv == PRINTF_CONV_DECIMAL)
    {
        if (sign)
        {
            if ((fmt->flags & PRINTF_FLAG_ALWAYS_SIGN) == PRINTF_FLAG_ALWAYS_SIGN)
            {
                if (out->out (out, '+') == EOF)
                    return EOF;
            }
        }
        else
        {
            if (out->out (out, '-') == EOF)
                return EOF;
        }
    }
    // Write precision characters
    if (accountPrecision)
    {
        for (int i = 0; i < precisionChars; ++i)
        {
            if (out->out (out, '0') == EOF)
                return EOF;
        }
    }
    // Write actual string.
    if (__outString (out, s, charsToPrint) == EOF)
        return EOF;
    // If being left justified, now write out field width stuff
    if ((fmt->flags & PRINTF_FLAG_LEFT_JUSTIFY) == PRINTF_FLAG_LEFT_JUSTIFY)
    {
        for (int i = 0; i < widthChars; ++i)
        {
            if (out->out (out, fieldWidthChar) == EOF)
                return EOF;
        }
    }
    return 0;
}

static void __getDataArg (_printfFmt_t* fmt, va_list* ap)
{
    switch (fmt->type)
    {
        case PRINTF_SIZE_SCHAR:
            fmt->sdata = (signed char) va_arg (*ap, int);
            break;
        case PRINTF_SIZE_UCHAR:
            fmt->udata = (unsigned char) va_arg (*ap, unsigned int);
            break;
        case PRINTF_SIZE_SSHORT:
            fmt->sdata = (signed short int) va_arg (*ap, int);
            break;
        case PRINTF_SIZE_USHORT:
            fmt->udata = (unsigned short int) va_arg (*ap, unsigned int);
            break;
        case PRINTF_SIZE_SINT:
            fmt->sdata = va_arg (*ap, int);
            break;
        case PRINTF_SIZE_UINT:
            fmt->udata = va_arg (*ap, unsigned int);
            break;
        case PRINTF_SIZE_SLONG:
            fmt->sdata = va_arg (*ap, signed long);
            break;
        case PRINTF_SIZE_ULONG:
            fmt->udata = va_arg (*ap, unsigned long);
            break;
        case PRINTF_SIZE_SLONGLONG:
            fmt->sdata = va_arg (*ap, signed long long);
            break;
        case PRINTF_SIZE_ULONGLONG:
            fmt->udata = va_arg (*ap, unsigned long long);
            break;
        case PRINTF_SIZE_PCHAR:
            fmt->udata = (unsigned char) va_arg (*ap, int);
            break;
        case PRINTF_SIZE_INTMAX:
            fmt->sdata = va_arg (*ap, intmax_t);
            break;
        case PRINTF_SIZE_UINTMAX:
            fmt->udata = va_arg (*ap, uintmax_t);
            break;
        case PRINTF_SIZE_PTRDIFF:
            fmt->sdata = va_arg (*ap, ptrdiff_t);
            break;
        case PRINTF_SIZE_SIZET:
            fmt->udata = va_arg (*ap, size_t);
            break;
        case PRINTF_SIZE_STRING:
            fmt->ptr = (uintmax_t) va_arg (*ap, char*);
            break;
        case PRINTF_SIZE_UINTPTR:
            fmt->ptr = (uintptr_t) va_arg (*ap, void*);
            break;
    }
}

static int __parseFormat (_printfOut_t* outData, _printfFmt_t* fmtRes, const char* fmt, va_list* ap)
{
    int fmtOffset = 0;
    // Parse flags first
    bool isFlag = true;
    while (isFlag)
    {
        switch (*fmt)
        {
            case '-':
                fmtRes->flags |= PRINTF_FLAG_LEFT_JUSTIFY;
                break;
            case '+':
                fmtRes->flags |= PRINTF_FLAG_ALWAYS_SIGN;
                fmtRes->flags &= ~(PRINTF_FLAG_SPACE_SIGN);
                break;
            case ' ':
                if ((fmtRes->flags & PRINTF_FLAG_ALWAYS_SIGN) != PRINTF_FLAG_ALWAYS_SIGN)
                {
                    fmtRes->flags |= PRINTF_FLAG_SPACE_SIGN;
                }
                break;
            case '#':
                fmtRes->flags |= PRINTF_FLAG_PREFIX;
                break;
            case '0':
                fmtRes->flags |= PRINTF_FLAG_0PAD;
                break;
            default:
                isFlag = false;
                break;
        }
        // If we found a flag, move to next format character
        if (isFlag)
        {
            INC_FORMAT
        }
    }
    // Parse field width
    if (*fmt == '*')
    {
        fmtRes->width = (int) va_arg (*ap, int);
        INC_FORMAT
    }
    // Check if it's a numeric field width
    if (IS_DIGIT1TO9 (*fmt))
        fmtRes->width = __fmtStrToNum (&fmt, &fmtOffset);
    // Get precision
    if (*fmt == '.')
    {
        INC_FORMAT
        fmtRes->precisionIsDefault = false;
        // Check if this in va_list
        if (*fmt == '*')
        {
            fmtRes->precision = (int) va_arg (*ap, int);
            INC_FORMAT
        }
        else
        {
            // Convert to number, if there is a number
            if (IS_DIGIT1TO9 (*fmt))
                fmtRes->precision = __fmtStrToNum (&fmt, &fmtOffset);
            else
                fmtRes->precision = 0;    // If precision is blank, set it 0
        }
    }
    // Get length modifier
    int lenMod = 0;
    switch (*fmt)
    {
        case 'h':
            // Check if this is h or hh
            if (*(fmt + 1) == 'h')
            {
                lenMod = PRINTF_LEN_CHAR;
                INC_FORMAT
            }
            else
                lenMod = PRINTF_LEN_SHORT;
            INC_FORMAT
            break;
        case 'l':
            if (*(fmt + 1) == 'l')
            {
                lenMod = PRINTF_LEN_LONG_LONG;
                INC_FORMAT
            }
            else
                lenMod = PRINTF_LEN_LONG;
            INC_FORMAT
            break;
        case 'j':
            lenMod = PRINTF_LEN_INTMAX;
            INC_FORMAT
            break;
        case 'z':
            lenMod = PRINTF_LEN_SIZET;
            INC_FORMAT
            break;
        case 't':
            lenMod = PRINTF_LEN_PTRDIFF;
            INC_FORMAT
            break;
    }
    // Get conversion specifier
    switch (*fmt)
    {
        case 'd':
        case 'i':
            fmtRes->conv = PRINTF_CONV_DECIMAL;
            // Get size
            fmtRes->type = lenToSizeInt[lenMod];
            break;
        case 'o':
            fmtRes->conv = PRINTF_CONV_OCTAL;
            goto szUnsigned;
        case 'x':
            fmtRes->conv = PRINTF_CONV_HEX_LOWER;
            goto szUnsigned;
        case 'X':
            fmtRes->conv = PRINTF_CONV_HEX_UPPER;
            goto szUnsigned;
        case 'u':
            fmtRes->conv = PRINTF_CONV_UNSIGNED;
            goto szUnsigned;
        szUnsigned:
            // Get size of type
            fmtRes->type = lenToSizeUint[lenMod];
            break;
        case 'c':
            fmtRes->conv = PRINTF_CONV_CHAR;
            fmtRes->type = PRINTF_SIZE_PCHAR;
            break;
        case 's':
            fmtRes->conv = PRINTF_CONV_STRING;
            fmtRes->type = PRINTF_SIZE_STRING;
            break;
        case 'p':
            fmtRes->conv = PRINTF_CONV_PTR;
            fmtRes->type = PRINTF_SIZE_UINTPTR;
            break;
        case 'n':
            fmtRes->conv = PRINTF_CONV_WRITTEN_CHARS;
            fmtRes->type = lenToSizeInt[lenMod];
            break;
        default:
            return fmtOffset;
    }
    INC_FORMAT
    // Set data
    __getDataArg (fmtRes, ap);
    return fmtOffset;
}

int vprintfCore (_printfOut_t* outData, const char* fmt, va_list ap)
{
    // Copy va_list
    va_list ap2;
    va_copy (ap2, ap);
    while (*fmt)
    {
        // Determine what the current character does
        switch (*fmt)
        {
            case '%':
                // Figure out if the next character is another percent sign
                if (fmt[1] == '%')
                {
                    if (outData->out (outData, '%') == EOF)
                        return EOF;
                    fmt += 2;
                    break;
                }
                ++fmt;
                // Parse format string
                _printfFmt_t fmtParse = {0};
                fmtParse.precision = 1;    // Default precision
                fmtParse.precisionIsDefault = true;
                int fmtOffset = __parseFormat (outData, &fmtParse, fmt, &ap2);
                if (fmtOffset == EOF)
                    return EOF;
                fmt += fmtOffset;
                // Format and print the argument
                if (__printArg (&fmtParse, outData) == EOF)
                    return EOF;
                break;
            default:
                if (outData->out (outData, *fmt) == EOF)
                    return EOF;
                ++fmt;
                break;
        }
    }
    return outData->charsPrinted;
}
