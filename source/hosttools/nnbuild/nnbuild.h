/*
    nnbuild.h - contains nnbuild header stuff
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

/// @file nnbuild.h

#ifndef _NNBUILD_H
#define _NNBUILD_H

#include <config.h>
#include <libintl.h>
#include <libnex.h>
#include <stdbool.h>
#include <stdint.h>

#define ACTION_BUFSIZE 1524

/// A package group
typedef struct _pkggroup
{
    Object_t obj;             ///< The object underlying this package group
    char32_t* name;           ///< Name of this package group
    ListHead_t* packages;     ///< The packages contained within
    ListHead_t* subGroups;    ///< Sub groups of this group
} packageGroup_t;

/// A package
typedef struct _package
{
    Object_t obj;                            ///< The object underlying this package
    char32_t* name;                          ///< The name of this package
    char downloadAction[ACTION_BUFSIZE];     ///< The action when downloading
    char configureAction[ACTION_BUFSIZE];    ///< Same, but for configuring
    char buildAction[ACTION_BUFSIZE];        ///< ... and so on
    char installAction[ACTION_BUFSIZE];
    char cleanAction[ACTION_BUFSIZE];
    char confHelpAction[ACTION_BUFSIZE];
    ListHead_t* depends;    //< Dependencies of this package
    bool isBuilt;           ///< If this package has been buit or not
    bool isInstalled;       ///< If this package has been built yet
    bool bindInstall;       ///< If installation and building should be one step
} package_t;

/// Converts the parse tree into the package tree
int buildPackageTree (ListHead_t* head);

/// Finds a package in the list
package_t* findPackage (char32_t* pkg);

/// Handles the build process
int buildPackages (int groupOrPkg, char* name, char* action);

/// Builds specified group
int buildGroup (packageGroup_t* group, char* action);

/// Builds one package
int buildPackage (package_t* package, char* action);

// Deletes package tree
void freePackageTree();

#endif
