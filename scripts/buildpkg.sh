#!/bin/sh
# buildpkg.sh - contains buildpkg infrastructure
# Copyright 2022 - 2024 The NexNix Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Get arguments
package=$2
action=$1

# Panics with a message and then exits
panic()
{
    echo "$0: error: $1"
    exit 1
}

# Checks return value and panics if an error occured
checkerr()
{
    # Check if return value is success or not
    if [ $1 -ne 0 ]
    then
        panic "$2"
    fi
}

if [ ! -f $NNPKGROOT/$package/$package.sh ]
then
    panic "package $package doesn't exist"
fi

# Source in package script
. $NNPKGROOT/$package/$package.sh

# Source build system in
if [ "$pkg_buildsys" = "cmake" ]
then
    . $NNPKGROOT/templates/cmakebuild.sh
fi

. $NNPKGROOT/templates/basetempl.sh

# If we don't recognize pkg_buildsys than the package script has it's own build system
# Or the package script is badly wrong. We assume case one

# Run action
if [ "$action" = "download" ]
then
    if [ "$pkg_download" = "1" ]
    then
        download
    fi
elif [ "$action" = "configure" ]
then
    if [ "$NNBUILDONLY" != "1" ]
    then
        download
    fi
    configure
elif [ "$action" = "build" ]
then
    build
    [ ! -z "$(type writeconf | grep "function")" ] && writeconf           # Writes out configuration files
    if [ -f $NNPKGROOT/${pkg_name}/nnpkg-pkg.conf ]
    then
        nnpkg add $NNPKGROOT/${pkg_name}/nnpkg-pkg.conf -c $NNCONFROOT/nnpkg.conf \
        || true
    fi
elif [ "$action" = "clean" ]
then
    clean
elif [ "$action" = "confhelp" ]
then
    confhelp
else
    panic "unrecognized action $action"
fi

