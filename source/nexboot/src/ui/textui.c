/*
    textui.c - contains text UI backend
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
#include <nexboot/drivers/terminal.h>
#include <nexboot/nexboot.h>
#include <nexboot/ui.h>
#include <string.h>

static NbObject_t* uiObj = NULL;    // We only support one UI

extern NbObjSvc textUiSvcs[];
extern NbObjSvcTab_t textUiSvcTab;
extern NbDriver_t textUiDrv;

#define TEXTUI_BKGD_COLOR NB_UI_COLOR_BLACK

// Text UI driver entry
static bool TextUiEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* console = params;
            assert (console);
            assert (console->interface == OBJ_INTERFACE_CONSOLE);
            char buf[64];
            NbLogMessage ("nexboot: Attaching text UI /Interfaces/TextUi to object %s\n",
                          NEXBOOT_LOGLEVEL_DEBUG,
                          NbObjGetPath (console, buf, 64));
            // Initialize UI structure
            NbUi_t* ui = malloc (sizeof (NbUi_t));
            // Get dimensions
            NbConsoleSz_t consoleSz = {0};
            NbObjCallSvc (console, NB_CONSOLE_GET_SIZE, &consoleSz);
            ui->height = consoleSz.rows;
            ui->width = consoleSz.cols;
            ui->root = NULL;
            ui->output = NbObjRef (console);
            // Set owner
            NbObjNotify_t notify = {0};
            notify.code = NB_CONSOLE_NOTIFY_SETOWNER;
            notify.data = &textUiDrv;
            NbObjCallSvc (console, OBJ_SERVICE_NOTIFY, &notify);
            // Clear the console
            NbObjCallSvc (console, NB_CONSOLE_CLEAR, NULL);
            // Disable cursor
            NbObjCallSvc (console, NB_CONSOLE_DISABLE_CURSOR, NULL);
            // Set background color
            NbObjCallSvc (console, NB_CONSOLE_SET_BGCOLOR, (void*) TEXTUI_BKGD_COLOR);
            // Create object
            uiObj = NbObjCreate ("/Interfaces/TextUi", OBJ_TYPE_UI, OBJ_INTERFACE_TEXTUI);
            if (!uiObj)
            {
                free (ui);
                return false;
            }
            // Install services
            NbObjSetData (uiObj, ui);
            NbObjInstallSvcs (uiObj, &textUiSvcTab);
            NbObjSetManager (uiObj, &textUiDrv);
            break;
        }
        case NB_DRIVER_ENTRY_DETACHOBJ: {
            NbUi_t* ui = NbObjGetData (uiObj);
            // Enable cursor
            NbObjCallSvc (ui->output, NB_CONSOLE_ENABLE_CURSOR, NULL);
            // Set color
            NbObjCallSvc (ui->output, NB_CONSOLE_SET_BGCOLOR, (void*) NB_UI_COLOR_BLACK);
            NbObjCallSvc (ui->output, NB_CONSOLE_SET_FGCOLOR, (void*) NB_UI_COLOR_WHITE);
            ui->output = NULL;
            // Destroy object
            NbObjDeRef (uiObj);
            break;
        }
    }
    return true;
}

static bool TextUiDumpData (void* objp, void* params)
{
    return true;
}

static bool TextUiNotify (void* objp, void* params)
{
    return true;
}

static void textUiWriteChar (NbUi_t* ui, char c, int x, int y)
{
    NbPrintChar_t pc;
    pc.c = c;
    pc.col = x;
    pc.row = y;
    NbObjCallSvc (ui->output, NB_CONSOLE_PRINTCHAR, &pc);
}

static void textUiSetColor (NbUi_t* ui, int fg, int bg)
{
    if (bg != NB_UI_COLOR_TRANSPARENT)
        NbObjCallSvc (ui->output, NB_CONSOLE_SET_BGCOLOR, (void*) bg);
    else
        NbObjCallSvc (ui->output, NB_CONSOLE_SET_BGCOLOR, (void*) TEXTUI_BKGD_COLOR);
    NbObjCallSvc (ui->output, NB_CONSOLE_SET_FGCOLOR, (void*) fg);
}

static void textUiOverwriteElement (NbUi_t* ui, NbUiElement_t* elem)
{
    // Set color
    textUiSetColor (ui, NB_UI_COLOR_WHITE, NB_UI_COLOR_BLACK);
    // Get absolute coordinates
    int x = elem->x;
    int y = elem->y;
    NbUiComputeCoords (elem->parent, &x, &y);
    // Overwite width * height
    for (int i = 0; i < elem->width; ++i)
    {
        for (int j = 0; j < elem->height; ++j)
        {
            textUiWriteChar (ui, ' ', x + i, j + y);
        }
    }
}

static void textUiDrawText (NbUi_t* ui, NbUiText_t* text)
{
    // Get absolute coordinates
    int x = text->elem.x;
    int y = text->elem.y;
    NbUiComputeCoords (text->elem.parent, &x, &y);
    // Get string
    char* str = StrRefGet (text->text);
    size_t strLen = strlen (str);
    int strIdx = 0;
    // Set color
    textUiSetColor (ui, text->elem.fgColor, text->elem.bgColor);
    // Start drawing
    for (int j = y; j < (y + text->elem.height) && strIdx < strLen; ++j)
    {
        for (int i = x; i < (x + text->elem.width); ++i, ++strIdx)
        {
            // Ensure we don't go past string
            if (strIdx >= strLen)
                break;
            textUiWriteChar (ui, str[strIdx], i, j);
        }
    }
}

static void textUiDrawMenuEntry (NbUi_t* ui, NbUiMenuEntry_t* menuEnt)
{
    // Get parent menu
    NbUiMenuBox_t* menu = (NbUiMenuBox_t*) menuEnt->elem.parent;
    assert (menu && menu->elem.type == NB_UI_ELEMENT_MENU);
    // Ensure we have a child
    if (!menuEnt->elem.child)
        return;
    NbUiText_t* text = (NbUiText_t*) menuEnt->elem.child;
    // Check if we are selected
    if (menuEnt->isSelected)
    {
        // Set BG and FG color accordingly
        text->elem.bgColor = NB_UI_COLOR_WHITE;
        text->elem.fgColor = NB_UI_COLOR_BLACK;
    }
    else
    {
        text->elem.bgColor = NB_UI_COLOR_TRANSPARENT;
        text->elem.fgColor = NB_UI_COLOR_WHITE;
    }
    // Draw text element
    textUiDrawText (ui, text);
}

static bool TextUiDrawElement (void* objp, void* param)
{
    NbObject_t* uiObj = objp;
    NbUi_t* ui = NbObjGetData (uiObj);
    NbUiElement_t* elem = param;
    // Determine how to draw element based on it's type
    if (elem->type == NB_UI_ELEMENT_TEXT)
    {
        NbUiText_t* text = (NbUiText_t*) elem;
        textUiDrawText (ui, text);
    }
    else if (elem->type == NB_UI_ELEMENT_MENUENT)
    {
        NbUiMenuEntry_t* entry = (NbUiMenuEntry_t*) elem;
        textUiDrawMenuEntry (ui, entry);
    }
    else if (!elem->type || elem->type == NB_UI_ELEMENT_MENU)
    {
        ;    // This element has no output
    }
    else
        return false;
    elem->invalid = false;
    return true;
}

static bool TextUiDestroyElement (void* objp, void* param)
{
    NbObject_t* uiObj = objp;
    NbUi_t* ui = NbObjGetData (uiObj);
    NbUiElement_t* elem = param;
    textUiOverwriteElement (ui, elem);
    return true;
}

// Object services
NbObjSvc textUiSvcs[] =
    {NULL, NULL, NULL, TextUiDumpData, TextUiNotify, TextUiDrawElement, TextUiDestroyElement};

NbObjSvcTab_t textUiSvcTab = {.numSvcs = ARRAY_SIZE (textUiSvcs), .svcTab = textUiSvcs};
NbDriver_t textUiDrv = {.name = "TextUi",
                        .deps = {0},
                        .devSize = 0,
                        .numDeps = 0,
                        .started = false,
                        .entry = TextUiEntry};
