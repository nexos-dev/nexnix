#[[
    NexTest.cmake - contains NexTest framework CMake functions
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

# Enables testing for the project
function(nextest_enable_tests)
    set(__NEXTEST_TESTS_ENABLED 1 PARENT_SCOPE)
endfunction()

# Creates a test for a library
function(nextest_add_library_test)
    if("${__NEXTEST_TESTS_ENABLED}" EQUAL "1")
        # Read in the arguments
        cmake_parse_arguments(__TESTARG "" "NAME;SOURCE" "DEFINES;LIBS;INCLUDES" ${ARGN})
        if(NOT __TESTARG_NAME OR NOT __TESTARG_SOURCE OR NOT __TESTARG_LIBS)
            message(FATAL_ERROR "Required argument missing")
        endif()
        string(TOLOWER ${__TESTARG_NAME} __TESTARG_EXENAME)

        # Create the executable and the CTest test if we are not cross compiling
        add_executable(${__TESTARG_EXENAME} ${__TESTARG_SOURCE})
        target_link_libraries(${__TESTARG_EXENAME} ${__TESTARG_LIBS})

        if(DEFINED __TESTARG_INCLUDES)
            target_include_directories(${__TESTARG_EXENAME} PRIVATE "${__TESTARG_INCLUDES}")
        endif()
        if(DEFINED __TESTARG_DEFINES)
            target_compile_definitions(${__TESTARG_EXENAME} PRIVATE ${__TESTARG_DEFINES})
        endif()
        
        # Check if we are using CTest
        if(NOT ${CMAKE_CROSSCOMPILING})
            add_test(NAME ${__TESTARG_NAME} COMMAND ${__TESTARG_EXENAME})
        else()
            # Move the files to the system root so they can be run in the target
            install(TARGETS ${__TARGET_EXENAME} DESTINATION ${CMAKE_INSTALL_BINDIR}/tests)
        endif()
    endif()
endfunction()
