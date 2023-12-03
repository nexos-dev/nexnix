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

static NbObject_t* uiObj = NULL;    // We only support one UI

extern NbObjSvc textUiSvcs[];
extern NbObjSvcTab_t textUiSvcTab;
extern NbDriver_t textUiDrv;

// Text UI driver entry
static bool TextUiEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* console = params;
            assert (console);
            assert (console->interface == OBJ_INTERFACE_CONSOLE);
            // Initialize UI structure
            NbUi_t* ui = malloc (sizeof (NbUi_t));
            // Get dimensions
            NbConsoleSz_t consoleSz = {0};
            NbObjCallSvc (console, NB_CONSOLEHW_GET_SIZE, &consoleSz);
            ui->height = consoleSz.rows;
            ui->width = consoleSz.cols;
            ui->root = NULL;
            ui->output = NbObjRef (console);
            // Set owner
            NbObjNotify_t notify = {0};
            notify.code = NB_CONSOLEHW_NOTIFY_SETOWNER;
            notify.data = &textUiDrv;
            NbObjCallSvc (console, OBJ_SERVICE_NOTIFY, &notify);
            // Clear the console
            NbObjCallSvc (console, NB_CONSOLEHW_CLEAR, NULL);
            // Create object
            uiObj = NbObjCreate ("/Interfaces/TextUi",
                                 OBJ_TYPE_UI,
                                 OBJ_INTERFACE_TEXTUI);
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

// Object services
NbObjSvc textUiSvcs[] = {NULL, NULL, NULL, TextUiDumpData, TextUiNotify};

NbObjSvcTab_t textUiSvcTab = {.numSvcs = ARRAY_SIZE (textUiSvcs),
                              .svcTab = textUiSvcs};
NbDriver_t textUiDrv = {.name = "TextUi",
                        .deps = {0},
                        .devSize = 0,
                        .numDeps = 0,
                        .started = false,
                        .entry = TextUiEntry};
