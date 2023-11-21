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
#include <nexboot/nexboot.h>
#include <nexboot/shell.h>

// Array of menu entries
static Array_t* menuEntries = NULL;

typedef struct _menuent
{
    StringRef_t* name;      // Name of entry
    ListHead_t* cmdLine;    // Entry commands
} MenuEntry_t;

#define MENU_ENTRY_MAX 32

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
