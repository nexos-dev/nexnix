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

#ifdef TOOLS_ENABLE_NLS
#define _(str)  dgettext ("nnbuild", str)
#define N_(str) (str)
#else
#define _(str)  (str)
#define N_(str) (str)
#endif

/// A package group
typedef struct _pkggroup
{
    char* name;
    struct _dependency* packages;    ///< The packages contained within
    struct _depgroup* subGroups;     ///< Sub groups of this group
    struct _pkggroup* next;          ///< The next package group
} packageGroup_t;

/// A dependency package group
typedef struct _depgroup
{
    packageGroup_t* group;     ///< The group being wrapped
    struct _depgroup* next;    ///< The next dependency group
} dependencyGroup_t;

/// A package
typedef struct _package
{
    char* name;                              ///< The name of this package
    char downloadAction[ACTION_BUFSIZE];     ///< The action when downloading
    char configureAction[ACTION_BUFSIZE];    ///< Same, but for configuring
    char buildAction[ACTION_BUFSIZE];        ///< ... and so on
    char installAction[ACTION_BUFSIZE];
    char cleanAction[ACTION_BUFSIZE];
    char confHelpAction[ACTION_BUFSIZE];
    struct _dependency* depends;    //< Dependencies of this package
    struct _package* next;          ///< Next package in list
    bool isBuilt;                   ///< If this package has been buit or not
    bool isInstalled;               ///< If this package has been built yet
    bool bindInstall;               ///< If installation and building should be one step
} package_t;

/// A dependency
typedef struct _dependency
{
    package_t* package;          ///< The package represented by this dependency
    struct _dependency* next;    ///< The next dependency
} dependency_t;

/// Converts the parse tree into the package tree
int buildPackageTree (ListHead_t* head);

/// Finds a package in the list
package_t* findPackage (char* pkg);

/// Handles the build process
int buildPackages (int groupOrPkg, char* name, char* action);

/// Builds specified group
int buildGroup (packageGroup_t* group, char* action);

/// Builds one package
int buildPackage (package_t* package, char* action);

#endif
