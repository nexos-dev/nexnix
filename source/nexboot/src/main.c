/*
    main.c - contains entry point
    Copyright 2022, 2023 The NexNix Project

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
#include <nexboot/detect.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/volume.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEXBOOT_CONF_FILE "nexboot.cfg"

void nbLaunchConf()
{
    // Find boot disk
    NbObject_t* bootDisk = NbFwGetBootDisk();
    if (!bootDisk)
    {
        // Error
        NbLogMessage ("nexboot: error: unable to find boot disk\n",
                      NEXBOOT_LOGLEVEL_EMERGENCY);
        NbShellLaunch (NULL);
    }
    // Find boot volume on this disk
    NbObject_t* bootVol = NbGetBootVolume (bootDisk);
    if (!bootVol)
    {
        // Error
        NbLogMessage ("nexboot: error: unable to find boot volume\n",
                      NEXBOOT_LOGLEVEL_CRITICAL);
        NbShellLaunch (NULL);
    }
    // Mount boot filesystem
    NbObject_t* fsObj = NbVfsMountFs (bootVol, "Boot");
    if (!fsObj)
    {
        // Error
        NbLogMessage ("nexboot: error: unable to mount boot partition\n",
                      NEXBOOT_LOGLEVEL_EMERGENCY);
        NbShellLaunch (NULL);
    }
    // Attempt to open configuration file
    NbFile_t* confFile = NbVfsOpenFile (fsObj, NEXBOOT_CONF_FILE);
    // Launch shell. Based on if parameter is NULL, it will either dump to shell
    // or run file
    NbShellLaunch (confFile);
    // If we get here, shell failed
    NbLogMessage ("nexboot: error: shell returned", NEXBOOT_LOGLEVEL_EMERGENCY);
    NbCrash();
}

// The main entry point into nexboot
void NbMain (NbloadDetect_t* nbDetect)
{
    //   So, we are loaded by nbload, and all it has given us is the nbdetect
    //   structure. It's our job to create a usable environment.
    //     Initialize logging
    NbLogInit();
    // Initialize memory allocation
    NbMemInit();
    // Initialize object database
    NbObjInitDb();
    // Create basic folders
    NbObjCreate ("/Interfaces", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
    NbObjCreate ("/Volumes", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
    NbObjCreate ("/Devices", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
    // Start phase 1 drivers
    if (!NbStartPhase1Drvs())
    {
        NbLogMessageEarly ("nexboot: error: Unable to start phase 1 drivers",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Detect hardware devices and add them to object database
    if (!NbFwDetectHw (nbDetect))
    {
        NbLogMessageEarly ("nexboot: error: Unable to detect hardware devices",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Start phase 2 of drivers
    if (!NbStartPhase2Drvs())
    {
        NbLogMessageEarly ("nexboot: error: Unable to start phase 2 drivers",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Start log
    NbLogInit2 (nbDetect);
    // Find boot partition and launch configuration script
    nbLaunchConf();
    assert (!"Shouldn't be here!");
}
