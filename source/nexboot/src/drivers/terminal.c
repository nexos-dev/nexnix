/*
    terminal.c - contains terminal driver
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

#include <assert.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

extern NbObjSvcTab_t terminalSvcTab;
extern NbDriver_t terminalDrv;

static NbTerminal_t* terms[32] = {NULL};
static int curTerm = 0;

static void createTerminal (int termNum,
                            NbTerminal_t* term,
                            NbObject_t* inEnd,
                            NbObject_t* outEnd)
{
    // Create device for terminal structure
    char buf[32] = {0};
    snprintf (buf, 32, "/Devices/Terminal%d", termNum);
    NbObject_t* obj = NbObjCreate (buf, OBJ_TYPE_DEVICE, OBJ_INTERFACE_TERMINAL);
    NbObjSetData (obj, term);
    NbObjInstallSvcs (obj, &terminalSvcTab);
    terms[curTerm] = term;
    ++curTerm;
    assert (curTerm < 32);
    term->outEnd = outEnd;
    term->inEnd = inEnd;
    NbObjNotify_t notify = {0};
    notify.data = &terminalDrv;
    // Get console size
    if (outEnd->interface == OBJ_INTERFACE_CONSOLE)
    {
        notify.code = NB_CONSOLEHW_NOTIFY_SETOWNER;
        NbConsoleSz_t sz = {0};
        NbObjCallSvc (term->outEnd, NB_CONSOLEHW_GET_SIZE, &sz);
        term->numCols = sz.cols;
        term->numRows = sz.rows;
        term->echo = true;
    }
    else if (outEnd->interface == OBJ_INTERFACE_RS232)
    {
        notify.code = NB_SERIAL_NOTIFY_SETOWNER;
        term->echo = true;
    }
    if (inEnd->interface == OBJ_INTERFACE_KBD)
        notify.code = NB_KEYBOARD_NOTIFY_SETOWNER;
    NbObjCallSvc (outEnd, OBJ_SERVICE_NOTIFY, &notify);
    if (inEnd != outEnd)
        NbObjCallSvc (inEnd, OBJ_SERVICE_NOTIFY, &notify);
}

static bool TerminalEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START: {
            // We need to create one terminal for each available console device
            // Find every console device
            NbObject_t* iter = NULL;
            NbObject_t* devDir = NbObjFind ("/Devices");
            NbTerminal_t* curTerm = NULL;
            NbObject_t* outEnd = NULL;
            NbObject_t* inEnd = NULL;
            NbObject_t* rewindTo =
                NULL;    // Where to go back to after finding a device
            bool foundConsole = false;    // Set when a primary display was found
            int curTermNum = 0;
            while ((iter = NbObjEnumDir (devDir, iter)) != 0)
            {
                if (iter->owner)
                    continue;
                if (iter->type == OBJ_TYPE_DEVICE)
                {
                    if ((iter->interface == OBJ_INTERFACE_CONSOLE && !outEnd &&
                         ((inEnd) ? (inEnd->interface != OBJ_INTERFACE_RS232)
                                  : true)) ||
                        (iter->interface == OBJ_INTERFACE_RS232 && !outEnd &&
                         ((inEnd) ? (inEnd->interface != OBJ_INTERFACE_KBD) : true)))
                    {
                        outEnd = iter;
                        if (!inEnd)
                        {
                            // Prepare terminal structure
                            curTerm = malloc (sizeof (NbTerminal_t));
                            memset (curTerm, 0, sizeof (NbTerminal_t));
                            assert (curTerm);
                            rewindTo = NULL;
                        }
                        else
                        {
                            if (!foundConsole &&
                                outEnd->interface == OBJ_INTERFACE_CONSOLE)
                            {
                                foundConsole = true;
                                curTerm->isPrimary = true;
                            }
                            createTerminal (curTermNum, curTerm, inEnd, outEnd);
                            ++curTermNum;
                            outEnd = NULL;
                            inEnd = NULL;
                            curTerm = NULL;
                            if (rewindTo)    // Ensure next iteration points here
                                iter = rewindTo->prevChild;
                        }
                    }
                    else if (inEnd && iter->interface != inEnd->interface)
                    {
                        rewindTo = iter;
                    }
                    if ((iter->interface == OBJ_INTERFACE_KBD && !inEnd &&
                         ((outEnd) ? (outEnd->interface != OBJ_INTERFACE_RS232)
                                   : true)) ||
                        (iter->interface == OBJ_INTERFACE_RS232 && !inEnd &&
                         ((outEnd) ? (outEnd->interface != OBJ_INTERFACE_CONSOLE)
                                   : true)))
                    {
                        inEnd = iter;
                        if (!outEnd)
                        {
                            // Prepare terminal structure
                            curTerm = malloc (sizeof (NbTerminal_t));
                            memset (curTerm, 0, sizeof (NbTerminal_t));
                            assert (curTerm);
                            rewindTo = NULL;
                        }
                        else
                        {
                            if (!foundConsole &&
                                outEnd->interface == OBJ_INTERFACE_CONSOLE)
                            {
                                foundConsole = true;
                                curTerm->isPrimary = true;
                            }
                            createTerminal (curTermNum, curTerm, inEnd, outEnd);
                            ++curTermNum;
                            outEnd = NULL;
                            inEnd = NULL;
                            curTerm = NULL;
                            if (rewindTo)
                                iter = rewindTo->prevChild;
                        }
                    }
                    else if (outEnd && iter->interface != outEnd->interface)
                    {
                        rewindTo = iter;
                    }
                }
            }
            break;
        }
        case NB_DRIVER_ENTRY_DETACHOBJ: {
            NbObject_t* obj = params;
            // Determine if this is an output or input device
            if (obj->interface == OBJ_INTERFACE_CONSOLE ||
                obj->interface == OBJ_INTERFACE_RS232)
            {
                // Find terminal in terms
                bool objFound = false;
                int i = 0;
                for (; i < curTerm; ++i)
                {
                    if (terms[i]->outEnd == obj)
                    {
                        objFound = true;
                        break;
                    }
                }
                assert (objFound);
                // Detach it
                terms[i]->outEnd = NULL;
            }
            if (obj->interface == OBJ_INTERFACE_KBD ||
                obj->interface == OBJ_INTERFACE_RS232)
            {
                // Find terminal in terms
                bool objFound = false;
                int i = 0;
                for (; i < curTerm; ++i)
                {
                    if (terms[i]->inEnd == obj)
                    {
                        objFound = true;
                        break;
                    }
                }
                assert (objFound);
                // Detach it
                terms[i]->inEnd = NULL;
            }
            break;
        }
    }
    return true;
}

static bool TerminalDumpData (void* objp, void* params)
{
    return true;
}

static bool TerminalNotify (void* objp, void* params)
{
    return true;
}

static void terminalScroll (NbTerminal_t* term)
{
    // Check if we need to scroll
    if (term->row >= term->numRows)
    {
        int rowsToScroll = (term->row - term->numRows) + 1;
        for (int i = 0; i < rowsToScroll; ++i)
            NbObjCallSvc (term->outEnd, NB_CONSOLEHW_SCROLL_DOWN, NULL);
        term->row = term->numRows - 1;
    }
}

static void terminalMoveCursor (NbTerminal_t* term)
{
    assert (term);
    // Move cursor to new position
    NbConsoleLoc_t loc = {0};
    loc.col = term->col;
    loc.row = term->row;
    NbObjCallSvc (term->outEnd, NB_CONSOLEHW_MOVE_CURSOR, &loc);
}

static void terminalProccesEscCodeLetter (NbTerminal_t* term, uint8_t c)
{
    // Determine what letter this is
    if (c == 'H' || c == 'f')
    {
        // Check if we have a row and column
        int row = 0, col = 0;
        if (term->escParams[0] && term->escParams[1])
        {
            row = term->escParams[0];
            col = term->escParams[1];
        }
        else if (term->escPos == 1)
        {
            // Only row was provided, which is invalid
            term->escState = 0;
            return;
        }
        term->row = row;
        term->col = col;
    }
    else if (c == 'A')
    {
        int count = 1;
        if (term->escParams[0])
            count = term->escParams[0];
        // Ensure no more paramters were provided
        if (term->escPos > 1)
        {
            term->escState = 0;
            return;
        }
        term->row -= count;
        if (term->row < 0)
            term->row = 0;
    }
    else if (c == 'B')
    {
        int count = 1;
        if (term->escParams[0])
            count = term->escParams[0];
        // Ensure no more paramters were provided
        if (term->escPos > 1)
        {
            term->escState = 0;
            return;
        }
        term->row += count;
    }
    else if (c == 'C')
    {
        int count = 1;
        if (term->escParams[0])
            count = term->escParams[0];
        // Ensure no more paramters were provided
        if (term->escPos > 1)
        {
            term->escState = 0;
            return;
        }
        term->col += count;
        if (term->col >= term->numCols)
        {
            ++term->row;
            term->col -= term->numCols;
        }
    }
    else if (c == 'D')
    {
        int count = 1;
        if (term->escParams[0])
            count = term->escParams[0];
        // Ensure no more paramters were provided
        if (term->escPos > 1)
        {
            term->escState = 0;
            return;
        }
        term->col -= count;
        if (term->col < 0)
        {
            // Negated addition here
            term->col = term->numCols + term->col;
            --term->row;
            if (term->row < 0)
                term->row = 0;
        }
    }
    else if (c == 'J')
    {
        int num = 0;
        if (term->escParams[0])
            num = term->escParams[0];
        // Ensure no more paramters were provided
        if (term->escPos > 1)
        {
            term->escState = 0;
            return;
        }
        if (num != 2)
        {
            // Not supported
            term->escState = 0;
            return;
        }
        // Clear screen
        NbObjCallSvc (term->outEnd, NB_CONSOLEHW_CLEAR, NULL);
        term->col = 0, term->row = 0;
        // Set color
        NbObjCallSvc (term->outEnd,
                      NB_CONSOLEHW_SET_BGCOLOR,
                      (void*) NB_CONSOLE_COLOR_BLACK);
        NbObjCallSvc (term->outEnd,
                      NB_CONSOLEHW_SET_FGCOLOR,
                      (void*) NB_CONSOLE_COLOR_WHITE);
    }
    else if (c == 'm')
    {
        // Grab attributes
        for (int i = 0; i < term->escPos; ++i)
        {
            int attr = term->escParams[i];
            if (attr >= 40)
                NbObjCallSvc (term->outEnd,
                              NB_CONSOLEHW_SET_BGCOLOR,
                              (void*) (attr - 40));
            else if (attr >= 30)
                NbObjCallSvc (term->outEnd,
                              NB_CONSOLEHW_SET_FGCOLOR,
                              (void*) (attr - 30));
            else if (attr == 0)
            {
                NbObjCallSvc (term->outEnd,
                              NB_CONSOLEHW_SET_BGCOLOR,
                              (void*) NB_CONSOLE_COLOR_BLACK);
                NbObjCallSvc (term->outEnd,
                              NB_CONSOLEHW_SET_FGCOLOR,
                              (void*) NB_CONSOLE_COLOR_WHITE);
            }
        }
    }
    term->escState = 0;
}

static bool terminalWriteChar (NbObject_t* termObj, uint8_t c)
{
    NbTerminal_t* term = termObj->data;
    NbObject_t* out = term->outEnd;
    if (!out)
        return false;
    assert (out->type == OBJ_TYPE_DEVICE);
    if (out->interface == OBJ_INTERFACE_RS232)
    {
        // Ensure CRLF is printed
        if (c == '\n')
            NbObjCallSvc (out, NB_SERIAL_WRITE, (void*) '\r');
        NbObjCallSvc (out, NB_SERIAL_WRITE, (void*) c);
    }
    else if (out->interface == OBJ_INTERFACE_CONSOLE)
    {
        if (term->escState)
        {
            // Determine where to go based on state. NOTE state equals last character
            if (term->escState == '\e')
            {
                term->escState = c;
                if (term->escState == 'D')
                {
                    // Move to next row
                    ++term->row;
                }
                else if (term->escState == 'M')
                {
                    // Move to last row
                    if (term->row)
                        --term->row;
                }
                else if (term->escState == 'H')
                {
                    // Move to next spot divisible by 4
                    term->col &= ~(4 - 1);
                    term->col += 4;
                    // Check for overflow
                    if (term->col >= term->numCols)
                    {
                        ++term->row;
                        term->col -= term->numCols;
                    }
                }
                else if (term->escState == '[')
                    ;
            }
            else if (term->escState == '[')
            {
                // Check for a number
                if (c >= '0' && c <= '9')
                {
                    term->escParams[term->escPos] = c - '0';
                    ++term->escPos;
                    term->numSize = 1;
                    assert (term->escPos < 16);
                    term->escState = c;
                }
                else
                    terminalProccesEscCodeLetter (term, c);
            }
            else if (term->escState >= '0' && term->escState <= '9')
            {
                if (c == ';')
                    term->escState = c;
                else if (c >= '0' && c <= '9')
                {
                    assert (!(term->numSize > 2));
                    term->escParams[term->escPos - 1] *= 10;
                    term->escParams[term->escPos - 1] += (c - '0');
                    ++term->numSize;
                }
                else
                    terminalProccesEscCodeLetter (term, c);
            }
            else if (term->escState == ';')
            {
                // Check for a number
                if (c >= '0' && c <= '9')
                {
                    term->escParams[term->escPos] = c - '0';
                    ++term->escPos;
                    term->numSize = 1;
                    assert (term->escPos < 16);
                    term->escState = c;
                }
                else
                    term->escState = 0;
            }
            else
                terminalProccesEscCodeLetter (term, c);
        }
        else
        {
            // Check if this character is special
            if (c == '\n' || c == '\r')
            {
                // Move to next line
                ++term->row;
                term->col = 0;
            }
            else if (c == '\t')
            {
                // Move to next spot divisible by 4
                term->col &= ~(4 - 1);
                term->col += 4;
                // Check for overflow
                if (term->col >= term->numCols)
                {
                    ++term->row;
                    term->col -= term->numCols;
                }
            }
            else if (c == '\b')
            {
                --term->col;
                if (term->col < 0)
                {
                    if (term->row == 0)
                        term->col = 0;    // Don't go off screen!
                    else
                    {
                        // Go back to last row
                        --term->row;
                        term->col = term->numCols - 1;
                    }
                }
            }
            else if (c == '\e')
            {
                // Prepare ourselves for escape processing
                term->escState = c;
                term->escPos = 0;
                term->numSize = 0;
            }
            else
            {
                // Print character out
                NbPrintChar_t pc = {0};
                pc.c = (char) c;
                pc.col = term->col;
                pc.row = term->row;
                NbObjCallSvc (out, NB_CONSOLEHW_PRINTCHAR, &pc);
                ++term->col;
                if (term->col >= term->numCols)
                {
                    // Wrap to next line
                    term->col = 0;
                    ++term->row;
                }
            }
        }
    end:
        // Scroll if needed
        terminalScroll (term);
        // Move cursor to new position
        terminalMoveCursor (term);
    }
    return true;
}

static uint8_t terminalReadChar (NbObject_t* termObj)
{
    // Determine how we should read
    NbTerminal_t* term = termObj->data;
    NbObject_t* in = term->inEnd;
    uint8_t c = 0;
    if (!in)
    {
        // No input end opened
        return 0;
    }
    assert (in->type == OBJ_TYPE_DEVICE);
    // Check if we have buffered data
    if (term->inBuf[term->bufPos])
    {
        ++term->bufPos;
        c = term->inBuf[term->bufPos - 1];
    }
    else
    {
        if (in->interface == OBJ_INTERFACE_RS232)
        {
        read:
            NbObjCallSvc (term->inEnd, NB_SERIAL_READ, &c);
            // Translate CR to LF
            if (term->foundCr && c == '\n')
            {
                term->foundCr = false;
                goto read;
            }
            if (c == '\r')
            {
                term->foundCr = true;
                c = '\n';
            }
        }
        else if (in->interface == OBJ_INTERFACE_KBD)
        {
            // Read character from keyboard
        readKey:
            NbKeyData_t keyData = {0};
            NbObjCallSvc (term->inEnd, NB_KEYBOARD_READ_KEY, &keyData);
            if (keyData.isBreak)
                goto readKey;
            // Check if this is an escape code
            if (keyData.isEscCode)
            {
                // Buffer this
                memset (term->inBuf, 0, 16);
                term->bufPos = 0;
                strcpy (term->inBuf, keyData.escCode + 1);
                c = *keyData.escCode;
            }
            else
                c = keyData.c;
        }
    }
    // Echo it if needed
    if (term->echo)
    {
        // Check if this is an escape sequence
        if (c == '\e')
        {
            if (!terminalWriteChar (termObj, '^'))
                return 0;
            terminalWriteChar (termObj, '[');
        }
        else
        {
            // Only print backspaces if they don't overwrite previous data
            if (c == '\b' &&
                (((term->col - 1) < 0) ? ((term->row - 1) < term->backMax[0])
                                       : false))
                ;
            else if (c == '\b' && (term->col - 1) < term->backMax[1] &&
                     term->row == term->backMax[0])
                ;
            else
            {
                if (!terminalWriteChar (termObj, c))
                    return 0;
            }
        }
    }
    return c;
}

static bool TerminalWrite (void* objp, void* params)
{
    NbObject_t* obj = objp;
    const char* s = params;
    while (*s)
    {
        if (!terminalWriteChar (obj, *s))
            return false;
        ++s;
    }
    return true;
}

static bool TerminalRead (void* objp, void* params)
{
    NbObject_t* termObj = objp;
    NbTerminal_t* term = termObj->data;
    NbTermRead_t* readData = params;
    // Ensure backspacing doesn't overwrite previous printing
    term->backMax[0] = term->row;
    term->backMax[1] = term->col;
    // Read data into buffer
    uint8_t c = 0;
    int charsWritten = 0;
    do
    {
        c = terminalReadChar (termObj);
        if (!c)
            return false;
        readData->buf[charsWritten] = (char) c;
        ++charsWritten;
    } while (c != '\n' && c != '\f' && readData->bufSz > (charsWritten - 1));
    return true;
}

static bool TerminalSetOpts (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbTerminal_t* term = obj->data;
    NbTerminal_t* in = params;
    assert (in);
    term->echo = in->echo;
    return true;
}

static bool TerminalGetOpts (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbTerminal_t* term = obj->data;
    NbTerminal_t* out = params;
    assert (out);
    memset (out, 0, sizeof (NbTerminal_t));
    // Set fields
    out->outEnd = term->outEnd;
    out->inEnd = term->inEnd;
    out->numCols = term->numCols;
    out->numRows = term->numRows;
    out->echo = term->echo;
    out->isPrimary = term->isPrimary;
    return true;
}

static bool TerminalClear (void* objp, void* unused)
{
    NbObject_t* obj = objp;
    assert (obj);
    NbTerminal_t* term = obj->data;
    if (term->outEnd->interface == OBJ_INTERFACE_CONSOLE)
    {
        term->row = 0, term->col = 0;
        NbObjCallSvc (term->outEnd, NB_CONSOLEHW_CLEAR, NULL);
    }
    return true;
}

static NbObjSvc terminalSvcs[] = {NULL,
                                  NULL,
                                  NULL,
                                  TerminalDumpData,
                                  TerminalNotify,
                                  TerminalWrite,
                                  TerminalRead,
                                  TerminalSetOpts,
                                  TerminalGetOpts,
                                  TerminalClear};

NbObjSvcTab_t terminalSvcTab = {ARRAY_SIZE (terminalSvcs), terminalSvcs};

NbDriver_t terminalDrv = {"Terminal", TerminalEntry, {0}, 0, false, 0};
