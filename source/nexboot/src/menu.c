/*
    menu.c - contains boot menu manager
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
#include <libnex/array.h>
#include <libnex/base.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <nexboot/ui.h>
#include <stdio.h>
#include <string.h>
#include <version.h>

// Array of menu entries
static Array_t* menuEntries = NULL;

typedef struct _menuent
{
    StringRef_t* name;           // Name of entry
    ListHead_t* cmdLine;         // Entry commands
    NbUiMenuEntry_t* menuEnt;    // UI menu entry
} MenuEntry_t;

#define MENU_ENTRY_MAX 15

// Menu UI element
static NbUiMenuBox_t* menu = NULL;

// Currently selected menu entry
static MenuEntry_t* selectedEnt = NULL;

// Keyboard object
static NbObject_t* keyboardObj = NULL;

// OS info we are booting from
static NbOsInfo_t* os = NULL;

// Adds a menu entry
void NbMenuAddEntry (StringRef_t* name, ListHead_t* cmdLine)
{
    if (!menuEntries)
        menuEntries =
            ArrayCreate (MENU_ENTRY_MAX, MENU_ENTRY_MAX, sizeof (MenuEntry_t));
    // Get entry from array
    size_t pos = ArrayFindFreeElement (menuEntries);
    MenuEntry_t* ent = ArrayGetElement (menuEntries, pos);
    assert (ent);
    // Initialize
    ent->name = StrRefNew (name);
    ent->cmdLine = cmdLine;
}

// boottype command
bool NbBootTypeMain (Array_t* args)
{
    // Ensure we have one arg
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("boottype: argument required\n");
        return false;
    }
    // Ensure there is an OS
    if (!os)
    {
        NbShellWrite ("boottype: OS not selected\n");
        return false;
    }
    // Ensure boottype hasn't already been set
    if (os->bootType)
    {
        NbShellWrite ("boottype: boot type already set\n");
        return false;
    }
    // Get element
    StringRef_t** arg = ArrayGetElement (args, 0);
    assert (arg);
    const char* osType = StrRefGet (*arg);
    // Find boot type
    size_t numTypes = ARRAY_SIZE (nbBootTypes);
    int bootType = 0;
    for (int i = 0; i < numTypes; ++i)
    {
        if (!strcmp (osType, nbBootTypes[i]))
        {
            bootType = i;
            break;
        }
    }
    // Check if boot type was found
    if (!bootType)
    {
        NbShellWrite ("boottype: invalid boot type\n");
        return false;
    }
    // Set it
    os->bootType = bootType;
    return true;
}

// payload command
bool NbPayloadMain (Array_t* args)
{
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("payload: argument required\n");
        return false;
    }
    if (!os)
    {
        NbShellWrite ("payload: OS not selected\n");
        return false;
    }
    if (os->payload)
    {
        NbShellWrite ("payload: payload already set\n");
        return false;
    }
    // Get element
    StringRef_t** elem = ArrayGetElement (args, 0);
    assert (elem);
    os->payload = StrRefNew (*elem);
    return true;
}

// bootargs command
bool NbBootArgsMain (Array_t* args)
{
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("bootargs: argument required\n");
        return false;
    }
    if (!os)
    {
        NbShellWrite ("bootargs: OS not selected\n");
        return false;
    }
    if (os->args)
    {
        NbShellWrite ("bootargs: arguments already set\n");
        return false;
    }
    // Ensure boot type is selected
    if (!os->bootType)
    {
        NbShellWrite ("bootargs: no boot type selected\n");
        return false;
    }
    // Ensure boot type supports it
    if (!(nbBootTypeFlags[os->bootType] & NB_BOOTTYPE_SUPPORTS_ARGS))
    {
        NbShellWrite ("bootargs: boot type doesn't support arguments");
        return false;
    }
    // Get element
    StringRef_t** elem = ArrayGetElement (args, 0);
    assert (elem);
    os->args = StrRefNew (*elem);
    return true;
}

// bootmod command
bool NbBootModMain (Array_t* args)
{
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("bootmod: argument required\n");
        return false;
    }
    if (!os)
    {
        NbShellWrite ("bootmod: OS not selected\n");
        return false;
    }
    // Ensure boot type is selected
    if (!os->bootType)
    {
        NbShellWrite ("bootmod: no boot type selected\n");
        return false;
    }
    // Ensure boot type supports it
    if (!(nbBootTypeFlags[os->bootType] & NB_BOOTTYPE_SUPPORTS_MODS))
    {
        NbShellWrite ("bootmod: boot type doesn't support modules");
        return false;
    }
    // Get element
    StringRef_t** elem = ArrayGetElement (args, 0);
    assert (elem);
    // Check if modules array is initialized
    if (!os->mods)
    {
        os->mods = ArrayCreate (NB_BOOT_MODS_INITIAL,
                                NB_BOOT_MODS_MAX,
                                sizeof (StringRef_t*));
        if (!os->mods)
        {
            NbShellWrite ("bootmod: out of memory");
            return false;
        }
    }
    // Add to array
    size_t idx = ArrayFindFreeElement (os->mods);
    if (idx == -1)
    {
        NbShellWrite ("bootmod: too many modules\n");
        return false;
    }
    // Grab element
    StringRef_t** modPtr = ArrayGetElement (os->mods, idx);
    // Copy pointer value to array
    *modPtr = StrRefNew (*elem);
    return true;
}

// boot command
bool NbBootMain (Array_t* args)
{
    UNUSED (args)
    // Ensure there is an OS
    if (!os)
    {
        NbShellWrite ("boot: OS not selected\n");
        return false;
    }
    // Ensure there is a boot type and payload
    if (!os->bootType)
    {
        NbShellWrite ("boot: OS type not selected");
        return false;
    }
    if (!os->payload)
    {
        NbShellWrite ("boot: payload not selected");
        return false;
    }
    // Call proper booting function based on boot type
    return nbBootTab[os->bootType](os);
}

// Finds a keyboard from object database
static void nbMenuGetKbd()
{
    NbObject_t* devDir = NbObjFind ("/Devices");
    assert (devDir);
    NbObject_t* iter = NULL;
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        // Check if this is a keyboard
        if (iter->interface == OBJ_INTERFACE_KBD)
        {
            keyboardObj = iter;
            break;
        }
    }
}

// Boots from selected OS entry
static void nbMenuBootOs()
{
    assert (selectedEnt);
    // Create OS data structure
    os = malloc (sizeof (NbOsInfo_t));
    if (!os)
    {
        NbLogMessage ("nexboot: out of memory\n", NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    memset (os, 0, sizeof (NbOsInfo_t));
    // Execute commands attached to OS
    NbShellExecute (selectedEnt->cmdLine);
    if (os->mods)
        ArrayDestroy (os->mods);
    free (os);             // If we get here, boot failed, free OS structure
    selectedEnt = NULL;    // De-select OS
    os = NULL;
}

// Selects a menu entry
static void nbMenuSelectEntry (MenuEntry_t* entry)
{
    // Un-select currently selected entry
    if (selectedEnt)
    {
        selectedEnt->menuEnt->isSelected = false;
        NbUiInvalidate (&selectedEnt->menuEnt->elem);
    }
    selectedEnt = entry;
    // Set it to selected
    selectedEnt->menuEnt->isSelected = true;
    NbUiInvalidate (&selectedEnt->menuEnt->elem);
    NbUiDrawElement (selectedEnt->menuEnt->elem.parent);
}

// Creates UI components
static bool nbMenuCreateUi (NbUi_t* ui)
{
    if (!menuEntries)
        return false;    // No menu entries created
    // Strings
    char hdrBuf[32] = {0};
    snprintf (hdrBuf, 32, "NexBoot Version %s", NEXNIX_VERSION);
    StringRef_t* hdr = StrRefCreate (hdrBuf);
    StrRefNoFree (hdr);
    StringRef_t* cmdText = StrRefCreate ("Press 'c' to enter command line");
    StrRefNoFree (cmdText);
    StringRef_t* instText = StrRefCreate (
        "Press 'up' and 'down' to move between entries, 'enter' to select");
    StrRefNoFree (instText);
    // Create main header
    NbUiCreateText (NULL,
                    hdr,
                    0,
                    1,
                    0,
                    0,
                    NB_UI_COLOR_WHITE,
                    NB_UI_COLOR_TRANSPARENT);
    // Create menu box
    menu = NbUiCreateMenuBox (NULL, 4, 4, ui->width - 4, MENU_ENTRY_MAX);
    // Create menu entries
    ArrayIter_t iterSt = {0};
    ArrayIter_t* iter = ArrayIterate (menuEntries, &iterSt);
    while (iter)
    {
        MenuEntry_t* menuEnt = iter->ptr;
        // Create entry
        menuEnt->menuEnt = NbUiAddMenuEntry (menu);
        // Create text
        NbUiCreateText ((NbUiElement_t*) menuEnt->menuEnt,
                        menuEnt->name,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0);
        iter = ArrayIterate (menuEntries, iter);
    }
    // Create instructions
    NbUiCreateText (NULL,
                    instText,
                    0,
                    menu->numElems + 5,
                    0,
                    0,
                    NB_UI_COLOR_WHITE,
                    NB_UI_COLOR_TRANSPARENT);
    // Create command prompt text
    NbUiCreateText (NULL,
                    cmdText,
                    0,
                    menu->numElems + 6,
                    0,
                    0,
                    NB_UI_COLOR_WHITE,
                    NB_UI_COLOR_TRANSPARENT);
    // Select first entry
    MenuEntry_t* firstEntry = ArrayGetElement (menuEntries, 0);
    nbMenuSelectEntry (firstEntry);
    return true;
}

// Reads from keyboard and selects an OS based on input
static bool nbMenuSelectOs (NbUi_t* ui)
{
    // Grab autoboot and check it
    StringRef_t* autoBoot = NbShellGetVar ("autoboot");
    if (!keyboardObj || !ui || (autoBoot && !strcmp (StrRefGet (autoBoot), "1")))
    {
        // Boot first menu entry
        selectedEnt = ArrayGetElement (menuEntries, 0);
        assert (selectedEnt);
        return true;
    }
    // Keyboard read loop
    NbKeyData_t key = {0};
    int curOs = 0;
    while (1)
    {
        NbObjCallSvc (keyboardObj, NB_KEYBOARD_READ_KEY, &key);
        // Determine what was pressed
        if (key.c == NB_KEY_UP)
        {
            // Move selected OS to left if we are able
            if (curOs > 0)
                --curOs;
            nbMenuSelectEntry (ArrayGetElement (menuEntries, curOs));
        }
        else if (key.c == NB_KEY_DOWN)
        {
            // Move to right in list if we are able
            if ((curOs + 1) < menu->numElems)
                ++curOs;
            nbMenuSelectEntry (ArrayGetElement (menuEntries, curOs));
        }
        else if (key.c == '\n')
        {
            // Return to boot
            return true;
        }
        else if (key.c == 'c')
        {
            // Return to terminal
            return false;
        }
    }
    return true;
}

// Initializes menu UI
bool NbMenuInitUi (Array_t* args)
{
    UNUSED (args);
    // Initialize UI backend
    NbUiInit();
    // Find a keyboard
    nbMenuGetKbd();
    // Grab object
    // NOTE: I would like to be portable to other have other UI backends, but for
    // right now we assume only TextUi exists
    NbObject_t* uiObj = NbObjFind ("/Interfaces/TextUi");
    NbUi_t* ui = NULL;
    if (uiObj)
    {
        ui = NbObjGetData (uiObj);
        // Initialize UI
        StringRef_t* autoBoot = NbShellGetVar ("autoboot");
        if (!(!keyboardObj || !ui ||
              (autoBoot && !strcmp (StrRefGet (autoBoot), "1"))))
        {
            if (!nbMenuCreateUi (ui))
            {
                ArrayDestroy (menuEntries);
                menuEntries = NULL;
                NbUiDestroy();
                return false;
            }
        }
    }
    // Select OS
    if (!nbMenuSelectOs (ui))
    {
        ArrayDestroy (menuEntries);
        menuEntries = NULL;
        NbUiDestroy();
        return false;    // Return to shell
    }
    // Destroy UI
    if (uiObj)
        NbUiDestroy();
    // Boot the OS
    nbMenuBootOs();
    // Boot returned, go to shell
    ArrayDestroy (menuEntries);
    menuEntries = NULL;
    return false;
}
