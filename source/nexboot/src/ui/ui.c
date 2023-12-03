/*
    ui.c - contains UI layer main interface
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
#include <nexboot/nexboot.h>
#include <nexboot/ui.h>

/// Contains if we evicted somebody to own console
static bool evicted = false;

/// Evicted driver
static NbDriver_t* evictedDrv = NULL;

// Saved UI object
static NbObject_t* uiObj = NULL;

/// Initialize UI system
bool NbUiInit()
{
    NbObject_t* devDir = NbObjFind ("/Devices");
    NbObject_t* iter = NULL;
    NbObject_t* savedIter = NULL;
    NbObject_t* foundConsole = NULL;
    // Iterate through /Devices and find a console
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (NbObjGetInterface (iter) == OBJ_INTERFACE_CONSOLE)
        {
            // If there is an owner, then we remember this, but we don't prefer it
            if (NbObjGetOwner (iter))
                savedIter = iter;
            else
            {
                // This is it
                foundConsole = iter;
                break;
            }
        }
    }
    // If we failed to find unowned console, then use a saved one. Otherwise, error
    // out
    if (!foundConsole)
    {
        if (!savedIter)
            return false;    // No suitable console
        evicted = true;
        evictedDrv = NbObjGetOwner (savedIter);
        foundConsole = savedIter;
    }
    // Start up UI driver
    if (foundConsole->interface == OBJ_INTERFACE_CONSOLE)
    {
        // Start driver
        NbDriver_t* tuiDrv = NbFindDriver ("TextUi");
        // Attach console to it
        NbSendDriverCode (tuiDrv, NB_DRIVER_ENTRY_ATTACHOBJ, foundConsole);
        // Grab interface
        uiObj = NbObjFind ("/Interfaces/TextUi");
        assert (uiObj);
    }
    return true;
}

// Destroys UI
void NbUiDestroy()
{
    assert (uiObj);
    // If there is an evicted owner, inform it that is now owns this object
    // Else simply detach object from UI
    NbUi_t* ui = NbObjGetData (uiObj);
    if (evicted)
    {
        evicted = false;
        NbSendDriverCode (evictedDrv, NB_DRIVER_ENTRY_ATTACHOBJ, ui->output);
    }
    else
        NbSendDriverCode (NbFindDriver ("TextUi"),
                          NB_DRIVER_ENTRY_DETACHOBJ,
                          ui->output);
}

/// Creates a UI text box
NbUiText_t* NbUiCreateText (NbUiElement_t* parent,
                            StringRef_t* str,
                            int fgColor,
                            int bgColor)
{
}

/// Creates a UI menu box
NbUiMenuBox_t* NbUiCreateMenuBox (NbUiElement_t* parent, int color)
{
}

/// Adds a menu entry to a menu box
NbUiMenuEntry_t* NbUiAddMenuEntry (NbUiMenuBox_t* menu, NbUiText_t* text)
{
}

/// Destroys a UI element
void NbUiDestroyElement (NbUiElement_t* elem)
{
}

/// Draws a UI element
void NbUiDrawElement (NbUiElement_t* elem)
{
}

/// Helper to compute absolute coordinates
void NbUiComputeCoords (NbUiElement_t* elem, int* x, int* y)
{
}
