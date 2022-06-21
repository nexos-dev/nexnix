#!/bin/sh
# mountpart.sh - mounts a partition
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
parttype=$2
imgfile=$3
partnum=$4
partfs=$5

# Checks if a command returned an error
checkerr()
{
    if [ "$1" != "0" ]
    then
        echo "$progname: $2"
        exit 1
    fi
}

if [ "$parttype" = "floppy" ] || [ "$parttype" = "ISO9660" ]
then
    if [ "$partfs" = "ISO9660" ]
    then
        # Create ISO mount directory
        mkdir -p $NNMOUNTDIR/../isomount
        exit 0
    fi
    if [ "$parttype" = "ISO9660" ]
    then
        imgfile=nnboot.img
    fi
    # FIXME: we use a hard coded loop device here so we don't have to communicate the
    # device path to umountpart.sh...
    loopdev=/dev/loop15
    sudo losetup /dev/loop15 $imgfile
    checkerr $? "unable to mount partition"
else
    # Set up the loopback device
    loopdev=$(sudo kpartx -avs $imgfile)
    checkerr $? "unable to mount partition"
    # Get just the partition device
    loopdev=/dev/mapper/$(echo "$loopdev" | awk -vline=$((partnum+1)) 'NR == line { print $3 }')
fi
# Mount it
mkdir -p $NNMOUNTDIR
sudo mount $loopdev $NNMOUNTDIR
