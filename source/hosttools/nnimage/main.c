/*
    main.c - contains entry point for nnimage
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

/// @file main.c

#include "nnimage.h"
#include <libconf/libconf.h>
#include <libnex.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

// The name of the program
static const char* progName = NULL;

// The file that is being read from
static const char* confName = "nnimage.conf";

// The action that is being performed
static const char* action = NULL;

// The file that is being output
static const char* outputName = NULL;

// Updat list file
static const char* listFile = NULL;

// If the image file should be overtwritten
static bool overwrite = false;

// Read the arguments passed
static int parseArgs (int argc, char** argv)
{
// The list of options that are valid
#define ARGS_VALIDARGS "f:o:hd:wl:"
    int arg = 0;
    while ((arg = getopt (argc, argv, ARGS_VALIDARGS)) != -1)
    {
        switch (arg)
        {
            case 'h':
                printf ("\
%s - image building helper\n\
Usage: %s [-h] [-f CONFFILE] [-o OUTPUT] [-d DIRECTORY] [-l FILELIST] [-w] ACTION\n\
Valid arguments:\n\
  -h\n\
              prints help and then exits\n\
  -f FILE\n\
              reads configuration from FILE\n\
  -o OUTPUT\n\
              outputs the image specfied in OUTPUT\n\
  -d DIRECTORY\n\
              directory where image data is\n\
  -l FILELIST\n\
              specifies a file containing a list of files to update\n\
  -w\n\
              specifies that if the image file specified in nnimage.conf\n\
              already exists, it should be overwitten without the user's\n\
              consent\n\
\n\
ACTION can be create, partition, update, or all. By default,\n\
configuration is read from nnimage.conf in the current directory\n",
                        progName,
                        progName);
                // Make sure that we exit doing nothing
                return 0;
            case 'f':
                confName = optarg;
                break;
            case 'o':
                outputName = optarg;
                break;
            case 'l':
                listFile = optarg;
                break;
            case 'w':
                overwrite = true;
                break;
            case '?':
                error ("unknown argument '%c'", optopt);
                return 0;
        }
    }
    return 1;
}

int main (int argc, char** argv)
{
    setprogname (argv[0]);
    if (!parseArgs (argc, argv))
        return 1;
    // Grab the action, first checking that it was passed
    if (argc == optind)
    {
        error ("action not specified");
        return 1;
    }

    action = argv[optind];
    if (!action)
    {
        // An error
        error ("action not specified");
        return 1;
    }
    // Parse the file
    ListHead_t* confBlocks = ConfInit (confName);
    if (!confBlocks)
        return 1;
    // Create the image list
    ListHead_t* images = createImageList (confBlocks);
    if (!images)
    {
        if (getImages())
            ListDestroy (getImages());
        ConfFreeParseTree (confBlocks);
        return 1;
    }
    // Create the image
    bool res = createImages (images, action, overwrite, outputName, listFile);
    ListDestroy (images);
    ConfFreeParseTree (confBlocks);
    return !res;
}
