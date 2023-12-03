/*
    ui.h - contains basic UI functions
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

#ifndef _UI_H
#define _UI_H

/// UI element structure
typedef struct _uielem
{
    int type;                  /// Type of element being represented
    int x;                     /// X position of top left corner
    int y;                     /// Y position of top left corner
    struct _uielem* parent;    /// Parent UI element
    struct _uielem* child;     /// First child element
    struct _uielem* left;      /// Left UI element in tree
    struct _uielem* right;     /// Right UI element in tree
} NbUiElement_t;

/// UI textbox element
typedef struct _uitext
{
    NbUiElement_t elem;    /// Element header
    StringRef_t* text;     /// Internal text
    int fgColor;           /// Foreground color
    int bgColor;           /// Background color. -1 for transparent
} NbUiText_t;

/// UI menubox element
typedef struct _uimenubox
{
    NbUiElement_t elem;    /// Element header
    int color;             /// Color of border
} NbUiMenuBox_t;

/// UI menubox entry element
typedef struct _uimenuentry
{
    NbUiElement_t header;    /// Element header
    bool isSelected;         /// Is it selected?
} NbUiMenuEntry_t;

/// UI info
typedef struct _ui
{
    NbObject_t* output;     /// Output device
    NbUiElement_t* root;    /// Root element of tree
    int width;              /// Width of UI
    int height;             /// Height of UI
} NbUi_t;

// Colors
#define NB_UI_COLOR_TRANSPARENT -1
#define NB_UI_COLOR_BLACK       0
#define NB_UI_COLOR_RED         1
#define NB_UI_COLOR_GREEN       2
#define NB_UI_COLOR_YELLOW      3
#define NB_UI_COLOR_BLUE        4
#define NB_UI_COLOR_MAGENTA     5
#define NB_UI_COLOR_CYAN        6
#define NB_UI_COLOR_WHITE       7

/// Initialize UI system
bool NbUiInit();

/// Destroys UI
void NbUiDestroy();

/// Creates a UI text box
NbUiText_t* NbUiCreateText (NbUiElement_t* parent,
                            StringRef_t* str,
                            int fgColor,
                            int bgColor);

/// Creates a UI menu box
NbUiMenuBox_t* NbUiCreateMenuBox (NbUiElement_t* parent, int color);

/// Adds a menu entry to a menu box
NbUiMenuEntry_t* NbUiAddMenuEntry (NbUiMenuBox_t* menu, NbUiText_t* text);

/// Destroys a UI element
void NbUiDestroyElement (NbUiElement_t* elem);

/// Draws a UI element
void NbUiDrawElement (NbUiElement_t* elem);

/// Helper to compute absolute coordinates
void NbUiComputeCoords (NbUiElement_t* elem, int* x, int* y);

// UI driver stuff

#endif
