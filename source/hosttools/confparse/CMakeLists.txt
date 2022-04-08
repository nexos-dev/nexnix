#[[
    CMakeLists.txt - contains confparse build system
    Copyright 2021, 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
]]

cmake_minimum_required(VERSION 3.00)
project(confparse VERSION 0.0.1)
enable_language(C)

# Link with libchardet
find_package(LibChardet REQUIRED)

list(APPEND CONF_SOURCES conf.c lex.c parse.c)

# Create the PO file lists
if(TOOLS_ENABLE_NLS)
    gettext_files(DOMAIN conf
                  OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/po" 
                  LANGS "${TOOLS_ENABLED_LOCALES}"
                  SOURCES "${CONF_SOURCES}")
endif()

# Create the library
add_library(conf ${CONF_SOURCES})

# Set SOName
set_target_properties(conf PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

# Link with libnex
target_link_libraries(conf PUBLIC LibNex::nex LibChardet::chardet)

# Install it
install(TARGETS conf)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/conf.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Setup test cases
list(APPEND CONF_TESTS lex parse)

foreach(test ${CONF_TESTS})
    nextest_add_library_test(NAME ${test}
                             SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}.c
                             LIBS conf
                             INCLUDES ${CONF_INCLUDE_DIRS}
                             WORKDIR ${CMAKE_CURRENT_SOURCE_DIR}/tests
                             LINK_LANG CXX)
endforeach()
