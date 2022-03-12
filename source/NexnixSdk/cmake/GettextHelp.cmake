#[[
    GettextHelp.cmake - contains helper functions to work with GNU gettext
    Copyright 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    There should be a copy of the License distributed in a file named
    LICENSE, if not, you may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License
]]

include(GNUInstallDirs)

# Find gettext programs
find_program(GETTEXT_XGETTEXT_PROGRAM "xgettext")
find_program(GETTEXT_MSGINIT_PROGRAM "msginit")
find_program(GETTEXT_MSGMERGE_PROGRAM "msgmerge")
find_program(GETTEXT_MSGFMT_PROGRAM "msgfmt")

# Check for GNU gettext
function(gettext_check var)
    find_program(${var} "xgettext")
endfunction()

# Sets up gettext chain of events. The signature looks like this:
# gettext_files (
#       DOMAIN domain
#       SOURCES files
#       LANGS langs
#       OUTPUT output
#       [SOURCE_LANG lang]
#       [KEYWORD key]
#       [INSTALL_DEST dest]
#)
# SOURCES equals the source files to extract strings from. LANGS is the locales to output.
# OUTPUT is the folder to put the .pot and .po files in. SOURCE_LANG is the language to pass to
# xgettext with the -L argument (defaults to C). KEYWORD is what to pass to gettext -k.
# INSTALL_DEST is where to install the PO files with CMake install()
function(gettext_files)
    # Check that every utility exists
    if (NOT GETTEXT_XGETTEXT_PROGRAM OR NOT GETTEXT_MSGINIT_PROGRAM 
            OR NOT GETTEXT_MSGMERGE_PROGRAM OR NOT GETTEXT_MSGFMT_PROGRAM)
        message(FATAL_ERROR "Required gettext utilities don't exist")
    endif()
    # Parse the arguments
    cmake_parse_arguments(__GETTEXT_ARG "" 
                          "DOMAIN;OUTPUT;SOURCE_LANG;INSTALL_DEST"
                          "SOURCES;LANGS;KEYWORDS" ${ARGN})
    if(NOT __GETTEXT_ARG_DOMAIN)
        message(FATAL_ERROR "Domain name required")
    elseif(NOT __GETTEXT_ARG_OUTPUT)
        message(FATAL_ERROR "PO Output folder required")
    elseif(NOT __GETTEXT_ARG_SOURCES)
        message(FATAL_ERROR "Sources required")
    elseif(NOT __GETTEXT_ARG_LANGS)
        message(FATAL_ERROR "Languages required")
    endif()

    # Set defaults where need be
    if(NOT __GETTEXT_ARG_SOURCE_LANG)
        set(__GETTEXT_ARG_SOURCE_LANG "C")
    endif()
    if(NOT __GETTEXT_ARG_KEYWORDS)
        set(__GETTEXT_ARG_KEYWORDS "_;N_")
    endif()
    if(NOT __GETTEXT_ARG_INSTALL_DEST)
        set(__GETTEXT_ARG_INSTALL_DEST "${CMAKE_INSTALL_DATADIR}/locale")
    endif()

    # Make directories
    if(NOT EXISTS ${__GETTEXT_ARG_OUTPUT})
        file(MAKE_DIRECTORY ${__GETTEXT_ARG_OUTPUT})
    endif()
    # Convert keyword specifiers to argument
    foreach(key ${__GETTEXT_ARG_KEYWORDS})
        list(APPEND __GETTEXT_KEYWORDS "-k${key}")
    endforeach()
    # Create .pot file if necessary
    if(NOT EXISTS ${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot)
        message(STATUS "Generating ${__GETTEXT_ARG_DOMAIN} POT file...")
        execute_process(COMMAND "${GETTEXT_XGETTEXT_PROGRAM}"
                        ${__GETTEXT_KEYWORDS}
                        "-L${__GETTEXT_ARG_SOURCE_LANG}"
                        "-o${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot"
                        ${__GETTEXT_ARG_SOURCES}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    # Command to create a .pot file from source file
    add_custom_command(OUTPUT ${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot
                       COMMAND "${GETTEXT_XGETTEXT_PROGRAM}"
                       ${__GETTEXT_ARG_SOURCES}
                       "-L${__GETTEXT_ARG_SOURCE_LANG}" ${__GETTEXT_KEYWORDS}
                       "-o${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot"
                       DEPENDS ${__GETTEXT_ARG_SOURCES}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # Go through every language
    foreach(__lang ${__GETTEXT_ARG_LANGS})
        if(NOT EXISTS ${__GETTEXT_ARG_OUTPUT}/${__lang})
            file(MAKE_DIRECTORY ${__GETTEXT_ARG_OUTPUT}/${__lang})
        endif()
        # Create initial PO file
        if(NOT EXISTS ${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po)
            execute_process(COMMAND "${GETTEXT_MSGINIT_PROGRAM}"
                            "-i" "${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot"
                            "-l ${__lang}" "-o${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po"
                          )
        endif()
        # PO target
        add_custom_command(OUTPUT "${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po"
                           COMMAND "${GETTEXT_MSGMERGE_PROGRAM}"
                           "-U" "${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po"
                           "${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot"
                           DEPENDS "${__GETTEXT_ARG_OUTPUT}/${__GETTEXT_ARG_DOMAIN}.pot")
        # And finally the MO file
        add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/CMakeFiles/translate.dir/${__lang}/${__GETTEXT_ARG_DOMAIN}.mo"
                           COMMAND "${CMAKE_COMMAND}" "-E"
                           "make_directory" "${CMAKE_BINARY_DIR}/CMakeFiles/translate.dir/${__lang}" "&&"
                           "${GETTEXT_MSGFMT_PROGRAM}"
                           "-o${CMAKE_BINARY_DIR}/CMakeFiles/translate.dir/${__lang}/${__GETTEXT_ARG_DOMAIN}.mo"
                           "${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po"
                           DEPENDS "${__GETTEXT_ARG_OUTPUT}/${__lang}/${__GETTEXT_ARG_DOMAIN}.po")
        list(APPEND __GETTEXT_DEPENDS 
                    "${CMAKE_BINARY_DIR}/CMakeFiles/translate.dir/${__lang}/${__GETTEXT_ARG_DOMAIN}.mo")
        # Setup the installation
        install(FILES "${CMAKE_BINARY_DIR}/CMakeFiles/translate.dir/${__lang}/${__GETTEXT_ARG_DOMAIN}.mo"
                DESTINATION "${__GETTEXT_ARG_INSTALL_DEST}/${__lang}/LC_MESSAGES")
    endforeach()
    
    # Create target
    add_custom_target(translate ALL
                      DEPENDS ${__GETTEXT_DEPENDS})
endfunction()
