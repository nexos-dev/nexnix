/*
    ui.c - contains UI layer main interface
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

#include <assert.h>
#include <nexboot/nexboot.h>
#include <nexboot/ui.h>
#include <string.h>

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
    // Create dummy UI element for root
    NbUi_t* ui = NbObjGetData (uiObj);
    NbUiElement_t* root = calloc (1, sizeof (NbUiElement_t));
    root->height = ui->height;
    root->width = ui->width;
    ui->root = root;
    return true;
}

void nbDestroyUiTree (NbUiElement_t* root)
{
    NbUiElement_t* iter = root;
    while (iter)
    {
        if (iter->child)
            nbDestroyUiTree (iter->child);
        NbUiElement_t* next = iter->right;
        if (iter->type == NB_UI_ELEMENT_TEXT)
        {
            // Destroy string
            NbUiText_t* text = (NbUiText_t*) iter;
            StrRefDestroy (text->text);
        }
        free (iter);
        iter = next;
    }
}

// Destroys UI
void NbUiDestroy()
{
    assert (uiObj);
    NbUi_t* ui = NbObjGetData (uiObj);
    // Destroy tree
    nbDestroyUiTree (ui->root);
    // If there is an evicted owner, inform it that is now owns this object
    // Else simply detach object from UI
    if (evicted)
    {
        evicted = false;
        NbSendDriverCode (evictedDrv, NB_DRIVER_ENTRY_ATTACHOBJ, ui->output);
    }
    else
        NbSendDriverCode (NbFindDriver ("TextUi"), NB_DRIVER_ENTRY_DETACHOBJ, ui->output);
}

// Adds element to tree
static void nbUiAddToTree (NbUiElement_t* elem, NbUiElement_t* parent)
{
    // Set parent
    elem->parent = parent;
    // Add to parent's child list
    elem->right = parent->child;
    if (parent->child)
        parent->child->left = elem;
    parent->child = elem;
}

// Adds element to tree on back of a child list
static void nbUiAddToTreeLast (NbUiElement_t* elem, NbUiElement_t* parent)
{
    // Set parent
    elem->parent = parent;
    // Add to parent's child list
    NbUiElement_t* iter = parent->child;
    if (!iter)
    {
        // List is empty, use normal method
        nbUiAddToTree (elem, parent);
    }
    else
    {
        // Go to end of list
        while (iter->right)
            iter = iter->right;
        // Add to list
        iter->right = elem;
        elem->left = elem;
        elem->right = NULL;
    }
}

// Re-draws invalid children
static void nbUiCheckInvalid (NbUiElement_t* elem)
{
    // Iterate through and check if any in tree are invalid
    NbUiElement_t* iter = elem;
    while (iter)
    {
        if (iter->child)
            nbUiCheckInvalid (elem->child);
        if (iter->invalid)
            NbObjCallSvc (uiObj, NB_UIDRV_DRAWELEM, iter);
        iter = iter->right;
    }
}

/// Creates a UI text box
NbUiText_t* NbUiCreateText (NbUiElement_t* parent,
                            StringRef_t* str,
                            int x,
                            int y,
                            int width,
                            int height,
                            int fgColor,
                            int bgColor)
{
    NbUi_t* ui = NbObjGetData (uiObj);
    NbUiText_t* elem = calloc (1, sizeof (NbUiText_t));
    if (!elem)
        return NULL;
    if (!parent)
        parent = ui->root;
    // Set fields
    elem->text = StrRefNew (str);
    elem->elem.child = NULL;
    // If bgColor or fgColor aren't set, inherit from parents
    if (bgColor)
        elem->elem.bgColor = bgColor;
    else
        elem->elem.bgColor = parent->bgColor;
    if (fgColor)
        elem->elem.fgColor = fgColor;
    else
        elem->elem.fgColor = parent->fgColor;
    // Get length of text
    size_t len = strlen (StrRefGet (str));
    // Set width and height
    if (!height)
        elem->elem.height = (len + (parent->width - 1)) / parent->width;
    else
        elem->elem.height = height;
    if (!width)
        elem->elem.width = (len > parent->width) ? parent->width : len;
    else
        elem->elem.width = width;
    // Set x and y
    if ((x + elem->elem.width) > parent->width)
    {
        StrRefDestroy (elem->text);
        free (elem);
        return NULL;
    }
    if ((y + elem->elem.height) > parent->height)
    {
        StrRefDestroy (elem->text);
        free (elem);
        return NULL;
    }
    elem->elem.x = x;
    elem->elem.y = y;
    elem->elem.type = NB_UI_ELEMENT_TEXT;
    // Add to tree
    nbUiAddToTree (&elem->elem, parent);
    // Set it to invalid and draw it
    elem->elem.invalid = true;
    NbUiDrawElement ((NbUiElement_t*) elem);
    return elem;
}

/// Creates a UI menu box
NbUiMenuBox_t* NbUiCreateMenuBox (NbUiElement_t* parent, int x, int y, int width, int height)
{
    NbUi_t* ui = NbObjGetData (uiObj);
    NbUiMenuBox_t* elem = calloc (1, sizeof (NbUiMenuBox_t));
    if (!elem)
        return NULL;
    if (!parent)
        parent = ui->root;
    // Set fields
    elem->numElems = 0;
    elem->elem.child = NULL;
    // Set width and height
    elem->elem.height = height;
    elem->elem.width = width;
    // Set x and y
    if ((x + elem->elem.width) > parent->width)
    {
        free (elem);
        return NULL;
    }
    if ((y + elem->elem.height) > parent->height)
    {
        free (elem);
        return NULL;
    }
    elem->elem.x = x;
    elem->elem.y = y;
    elem->elem.type = NB_UI_ELEMENT_MENU;
    // Add to tree
    nbUiAddToTree (&elem->elem, parent);
    // Set it to invalid and draw it
    elem->elem.invalid = true;
    NbUiDrawElement ((NbUiElement_t*) elem);
    return elem;
}

/// Adds a menu entry to a menu box
NbUiMenuEntry_t* NbUiAddMenuEntry (NbUiMenuBox_t* menu)
{
    assert (menu->elem.type == NB_UI_ELEMENT_MENU);
    NbUiElement_t* parent = &menu->elem;
    NbUiMenuEntry_t* elem = (NbUiMenuEntry_t*) calloc (1, sizeof (NbUiMenuEntry_t));
    elem->isSelected = false;
    elem->elem.type = NB_UI_ELEMENT_MENUENT;
    elem->elem.invalid = true;
    elem->elem.width = parent->width - 2;
    elem->elem.height = 1;
    elem->elem.x = 0;
    elem->elem.y = menu->numElems;
    elem->elem.fgColor = NB_UI_COLOR_WHITE;
    elem->elem.bgColor = NB_UI_COLOR_TRANSPARENT;
    // Increate number of elements in menu
    menu->numElems++;
    // Check if we are past max entries
    if (menu->numElems > menu->elem.height)
    {
        free (elem);
        return NULL;    // Can't create it
    }
    // Add to tree
    nbUiAddToTreeLast ((NbUiElement_t*) elem, parent);
    // Set it to invalid and draw it
    elem->elem.invalid = true;
    NbUiDrawElement ((NbUiElement_t*) elem);
    return elem;
}

/// Destroys a UI element
bool NbUiDestroyElement (NbUiElement_t* elem)
{
    assert (elem->parent);
    // Ensure there are no children
    if (elem->child)
        return false;
    // Remove from tree
    assert (elem->parent);
    if (elem->left)
        elem->left->right = elem->right;
    if (elem->right)
        elem->right->left = elem->left;
    if (elem->parent->child == elem)
        elem->parent->child = elem->right;
    // Call service
    NbObjCallSvc (uiObj, NB_UIDRV_DESTROYELEM, elem);
    // Invalidate parent
    NbUiInvalidate (elem->parent);
    NbUiDrawElement (elem->parent);
    return true;
}

/// Draws a UI element and children
void NbUiDrawElement (NbUiElement_t* elem)
{
    NbUi_t* ui = NbObjGetData (uiObj);
    if (!elem)
        elem = ui->root;
    // Check if children need redraw
    nbUiCheckInvalid (elem->child);
    // Redraw this element if needed
    if (elem->invalid)
        NbObjCallSvc (uiObj, NB_UIDRV_DRAWELEM, elem);
}

/// Helper to compute absolute coordinates
void NbUiComputeCoords (NbUiElement_t* elem, int* x, int* y)
{
    // Add all parents x, y pair
    while (elem)
    {
        *x += elem->x;
        *y += elem->y;
        elem = elem->parent;
    }
}
