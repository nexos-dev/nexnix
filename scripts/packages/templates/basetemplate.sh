#!/bin/sh
# basetemplate.sh - contains base functionallity
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

# Panics with a message and then exits
panic()
{
    echo "$(basename $0): error: $1"
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

# Downloads the source code
download()
{
    echo "Downloading $pkg_name..."
    if [ "$pkg_downloadType" = "git" ]
    then
        rm -rf $NNSOURCEROOT/$pkg_name
        git clone $pkg_downloadUrl $NNSOURCEROOT/$pkg_name
        checkerr $? "unable to download $pkg_name"
    elif [ "$pkg_downloadType" = "tarball" ]
    then
        mkdir -p $NNSOURCEROOT/tarballs
        rm -f $NNSOURCEROOT/tarballs/$(basename $pkg_downloadUrl)
        wget $pkg_downloadUrl -P $NNSOURCEROOT/tarballs
        checkerr $? "unable to download $pkg_name"
        tar -C $NNSOURCEROOT -xf $NNSOURCEROOT/tarballs/$(basename $pkg_downloadUrl)
        checkerr $? "unable to extract $pkg_name"
    fi
}
