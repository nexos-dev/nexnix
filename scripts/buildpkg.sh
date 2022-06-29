#!/bin/sh
# buildpkg.sh - builds a package in nexnix
# Copyright 2021, 2022 The NexNix Project
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

# Save arguments
action=$1
package=$2

# Panics with a message and then exits
panic()
{
    echo "$(basename $0): error: $1"
    exit 1
}

# Check if package exists
if [ ! -f "$NNPROJECTROOT/scripts/packages/$package.sh" ]
then
    panic "package $package doesn't exist"
fi
# Read in package script
. $NNPROJECTROOT/scripts/packages/$package.sh

# Read in build system script
if [ ! -f .$NNPROJECTROOT/scripts/packages/templates/${pkg_buildsystem}build.sh ]
then
    panic "build system ${pkg_buildsystem} doesn't exist"
fi

. $NNPROJECTROOT/scripts/packages/templates/${pkg_buildsystem}build.sh

# Execute action
if [ "$action" = "build" ]
then
    build
elif [ "$action" = "install" ]
then
    install
elif [ "$action" = "clean" ]
then
    clean
elif [ "$action" = "configure" ]
then
    configure
elif [ "$action" = "download" ]
then
    download
elif [ "$action" = "confhelp" ]
then
    confhelp
else
    echo "$(basename $0): invalid action \"$action\""
    exit 1
fi
