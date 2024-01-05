#!/bin/sh
# basetempl.sh - contains base build system template
# Copyright 2021 - 2024 The NexNix Project
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

# Downloads the source code
download()
{
    echo "Downloading $pkg_name..."
    if [ "$pkg_downloadType" = "git" ]
    then
        rm -rf $NNEXTSOURCEROOT/$pkg_name
        git clone $pkg_downloadUrl $NNEXTSOURCEROOT/$pkg_name
        checkerr $? "unable to download $pkg_name"
    elif [ "$pkg_downloadType" = "tarball" ]
    then
        mkdir -p $NNEXTSOURCEROOT/tarballs
        rm -f $NNEXTSOURCEROOT/tarballs/$(basename $pkg_downloadUrl)
        wget $pkg_downloadUrl -P $NNEXTSOURCEROOT/tarballs
        checkerr $? "unable to download $pkg_name"
        tar -C $NNEXTSOURCEROOT -xf $NNEXTSOURCEROOT/tarballs/$(basename $pkg_downloadUrl)
        checkerr $? "unable to extract $pkg_name"
    fi
}
