#!/bin/sh
# writeiso.sh - builds an ISO image with xorriso
# Copyright 2022 - 20244 The NexNix Project
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
output=$1
listfile=$2
bootimg=$3
bootmode=$4
bootemu=$5
isuniversal=$6
hybridmbr=$7

# Begin constructing command line
xorrisoargs="-as mkisofs -graft-points -iso-level 1 -path-list $listfile"
if [ "$bootmode" = "noboot" ]
then
    : ;
elif [ "$bootmode" = "bios" ]
then
    if [ "$bootemu" = "noemu" ]
    then
        bootimg=$hybridmbr
    fi
    xorrisoargs="$xorrisoargs -c boot.cat -b $bootimg"
    if [ "$bootemu" = "hdd" ]
    then
        xorrisoargs="$xorrisoargs -hard-disk-boot"
    elif [ "$bootemu" = "noemu" ]
    then
        xorrisoargs="$xorrisoargs -no-emul-boot -boot-info-table -boot-load-size 4"
    fi
    if [ "$isuniversal" = "true" ]
    then
        xorrisoargs="$xorrisoargs -isohybrid-mbr $hybridmbr"
    fi
    if [ "$bootmode" = "hybrid" ]
    then
        xorrisoargs="$xorrisoargs -eltorito-alt-boot"
    fi
elif [ "$bootmode" = "efi" ]
then
    xorrisoargs="$xorrisoargs -e $bootimg -no-emul-boot"
    if [ "$isuniversal" = "true" ]
    then
        xorrisoargs="$xorrisoargs -isohybrid-gpt-basdat"
    fi
fi

xorrisoargs="$xorrisoargs -o $output"
# Run it!
exec xorriso $xorrisoargs
