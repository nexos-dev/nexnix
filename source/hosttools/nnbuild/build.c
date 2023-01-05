/*
    build.c - contains build process manager for nnbuild
    Copyright 2021, 2022, 2023 The NexNix Project

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

// clang-format off
#include <unistd.h>
// clang-format on
#include "nnbuild.h"
#include <libconf/libconf.h>
#include <libgen.h>
#include <libnex/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

// Child process ID
static pid_t childPid = 0;

// Redirects signals from parent process to shell
static void signalHandler (int signalNum)
{
    error ("child exiting on signal %d", signalNum);
    // Kill child process
    kill (childPid, signalNum);
}

static int runShell (char* cmd, char* action)
{
    pid_t pid = fork();
    if (!pid)
    {
        char* shell = "/bin/sh";
        execl (shell, shell, "-ec", cmd, NULL);
        // An error occured if we got here
        _Exit (1);
    }
    else
    {
        childPid = pid;
        // Handle signals
        signal (SIGINT, signalHandler);
        signal (SIGQUIT, signalHandler);
        signal (SIGHUP, signalHandler);
        signal (SIGTERM, signalHandler);
        // Wait for the shell to finish
        int status = 0;
        wait (&status);
        // Exit if an error occured
        if (status)
        {
            error ("an error occured while invoking action \"%s\"", action);
            return 0;
        }
    }
    return 1;
}

static int doDownload (package_t* package)
{
    // Check if we should set up template
    if (package->useBuildPkg)
    {
        sprintf (package->downloadAction,
                 "$NNSCRIPTROOT/buildpkg.sh download %s",
                 UnicodeToHost (package->name));
    }
    // Run the command
    if (package->downloadAction[0])
        return runShell (package->downloadAction, "download");
    return 1;
}

static int doConfigure (package_t* package)
{
    // Check if we should set up template
    if (package->useBuildPkg)
    {
        sprintf (package->configureAction,
                 "$NNSCRIPTROOT/buildpkg.sh configure %s",
                 UnicodeToHost (package->name));
    }
    // Run the command
    if (package->configureAction[0])
        return runShell (package->configureAction, "configure");
    return 1;
}

static int doConfHelp (package_t* package)
{
    // Check if we should set up template
    if (package->useBuildPkg)
    {
        sprintf (package->confHelpAction,
                 "$NNSCRIPTROOT/buildpkg.sh confhelp %s",
                 UnicodeToHost (package->name));
    }
    if (package->confHelpAction[0])
        return runShell (package->confHelpAction, "confhelp");
    return 1;
}

static int doBuild (package_t* package)
{
    // Check if we should set up template
    if (package->useBuildPkg)
    {
        sprintf (package->buildAction,
                 "$NNSCRIPTROOT/buildpkg.sh build %s",
                 UnicodeToHost (package->name));
    }
    // Run the command
    if (package->buildAction[0])
        return runShell (package->buildAction, "build");
    return 1;
}

static int doClean (package_t* package)
{
    // Check if we should set up template
    if (package->useBuildPkg)
    {
        sprintf (package->cleanAction,
                 "$NNSCRIPTROOT/buildpkg.sh clean %s",
                 UnicodeToHost (package->name));
    }
    // Run the command
    if (package->cleanAction[0])
        return runShell (package->cleanAction, "clean");
    return 1;
}

// Builds one package
int buildPackage (package_t* package, char* action)
{
    //  Check if we are running confhelp action
    if (!strcmp (action, "confhelp"))
        return doConfHelp (package);
    // Check if we need to build this package
    if (package->isBuilt)
        return 1;
    // Go through all dependent packages
    ListEntry_t* curDepEntry = ListFront (package->depends);
    while (curDepEntry)
    {
        if (!buildPackage (ListEntryData (curDepEntry), action))
            return 0;
        curDepEntry = ListIterate (curDepEntry);
    }
    // Do the build, based on the action
    if (!strcmp (action, "download"))
    {
        package->isBuilt = 1;
        return doDownload (package);
    }
    else if (!strcmp (action, "configure"))
    {
        package->isBuilt = 1;
        return doConfigure (package);
    }
    else if (!strcmp (action, "confbuild"))
    {
        // In case a future package tries to build it
        package->isBuilt = 1;
        if (!doConfigure (package))
            return 0;
        return doBuild (package);
    }
    else if (!strcmp (action, "build"))
    {
        package->isBuilt = 1;
        return doBuild (package);
    }
    else if (!strcmp (action, "clean"))
    {
        package->isBuilt = 1;
        return doClean (package);
    }
    else if (!strcmp (action, "all"))
    {
        package->isBuilt = 1;
        if (!doDownload (package))
            return 0;
        if (!doConfigure (package))
            return 0;
        if (!doBuild (package))
            return 0;
    }
    else
    {
        error ("invalid action %s", action);
        return 0;
    }
    return 1;
}

// Builds a package group
int buildGroup (packageGroup_t* group, char* action)
{
    // Build all sub groups first
    ListEntry_t* curGroupEntry = ListFront (group->subGroups);
    while (curGroupEntry)
    {
        if (!buildGroup (ListEntryData (curGroupEntry), action))
            return 0;
        curGroupEntry = ListIterate (curGroupEntry);
    }
    // Build all packages
    ListEntry_t* curPkgEntry = ListFront (group->packages);
    while (curPkgEntry)
    {
        if (!buildPackage (ListEntryData (curPkgEntry), action))
            return 0;
        curPkgEntry = ListIterate (curPkgEntry);
    }
    return 1;
}
