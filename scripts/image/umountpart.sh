#!/bin/sh
# umountpart.sh - unmounts a partition
# Copyright 2022 The NexNix Project
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

progname=$1
image=$2
parttype=$3
partfs=$4

# Checks if a command returned an error
checkerr()
{
    if [ "$1" != "0" ]
    then
        echo "$progname: $2"
        exit 1
    fi
}

# Unmount it
if [ "$partfs" != "ISO9660" ]
then
    sudo umount $NNMOUNTDIR
    checkerr $? "unable to clean partition"
else
    rm -rf $NNMOUNTDIR/../isomount
    rm -rf $NNMOUNTDIR
    exit 0
fi
if [ "$parttype" = "floppy" ] || [ "$parttype" = "ISO9660" ]
then
    sudo losetup -d /dev/loop15
    checkerr $? "unable to clean partition"
else
    sudo kpartx -d $image
    checkerr $? "unable to clean partition"
fi

rm -rf $NNMOUNTDIR
