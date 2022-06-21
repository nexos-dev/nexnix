#!/bin/sh
# parttab.sh - create partition table
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

# nnimage's program name is in $3
progname=$3

# Checks if a command returned an error
checkerr()
{
    if [ "$1" != "0" ]
    then
        echo "$progname: $2"
        exit 1
    fi
}

trap 'exit 1' HUP TERM INT

echo "Creating partition table with format $1 on image $2..."

# Partition it with parted
if [ "$1" = "GPT" ]
then
    sudo parted -s $2 "mklabel gpt"
    checkerr $? "unable to create partition table"
elif [ "$1" = "MBR" ]
then
    sudo parted -s $2 "mklabel msdos"
    checkerr $? "unable to create partition table"
fi
