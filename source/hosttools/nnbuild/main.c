/*
    main.c - contains entry point for nnbuild
    Copyright 2022 The NexNix Project

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

/// @file main.c

#include "nnbuild.h"
#include <conf.h>
#include <libnex.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

// The package group to be built. Default is "all"
static char* pkgGroup = "all";

// The package to be built
static char* pkg = NULL;

// The action to perform
static char* action = NULL;

// The file to use
static char* confName = "nnbuild.conf";

// Parses arguments passed to nnbuild
static int parseArgs (int argc, char** argv)
{
// The list of arguments that are valid
#define VALIDOPTS _ ("g:p:hf:")
    int arg = 0;
    const char* progName = getprogname();
    while ((arg = getopt (argc, argv, VALIDOPTS)) != -1)
    {
        // Decide what to do based on the argument
        switch (arg)
        {
            case 'h':
                printf (_ ("\
%s - manages the build process of NexNix\n\
Usage: %s [-h] [-g PACKAGE_GROUP] [-p PACKAGE] [-f FILE] ACTION\n\
Valid Arguments:\n\
  -h\n\
             prints help and then exits\n\
  -g GROUP\n\
             builds the specified package group\n\
  -p PACKAGE\n\
             builds the specified package\n\
  -f FILE\n\
             reads configuration from the specified file\n\
\n\
ACTION can be either clean, download, configure, all, build, confbuild,\n\
or install.  The configuration gets read from the file nnbuild.conf in the\n\
current directory if a file isn't specified on the command line\n"),
                        progName,
                        progName);
                return 0;
            case 'g':
                pkgGroup = optarg;
                break;
            case 'p':
                pkg = optarg;
                break;
            case 'f':
                confName = optarg;
                break;
            case '?':
                return 0;
        }
    }
    return 1;
}

int main (int argc, char** argv)
{
    setprogname (argv[0]);
#ifdef TOOLS_ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain ("nnbuild", TOOLS_LOCALE_BASE);
#endif
    // Parse the arguments
    if (!parseArgs (argc, argv))
        return 1;

    // Check only one of package or package group was specified
    if (pkg && (strcmp (pkgGroup, _ ("all")) != 0))
    {
        error (_ ("package and group specification are mutually exclusive"));
        return 1;
    }
    // Check if package group should be null
    pkgGroup = (!strcmp (pkgGroup, _ ("all")) && pkg) ? NULL : pkgGroup;

    // Check the action
    if (argc == optind)
    {
        error (_ ("action not specified"));
        return 1;
    }
    // Grab the action
    action = argv[optind];
    if (!action)
    {
        error (_ ("action not specified"));
        return 1;
    }
    // Parse the file
    ListHead_t* confBlocks = ConfInit (confName);
    buildPackageTree (confBlocks);
    // Build the packages
    int res = 0;
    if (pkgGroup)
        res = buildPackages (0, pkgGroup, action);
    else if (pkg)
        res = buildPackages (1, pkg, action);
    ConfFreeParseTree (confBlocks);
    return res;
}
