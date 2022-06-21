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

# nnimage's program name is in $12
progname=${12}

parttype=$1
partfs=$2
imgname=$3
bootmode=$4
bootemu=$5
isbootpart=$6
partname=$7
partstart=$8
partsize=$9
partmul=${10}
partnum=${11}
mulsize=${13}
loopdev=

# Checks if a command returned an error
checkerr()
{
    if [ "$1" != "0" ]
    then
        echo "$progname: $2"
        exit 1
    fi
}

if [ "$parttype" = "ISO9660" ] || [ "$parttype" = "floppy" ]
then
    trap 'rm -f nnboot.img; [ ! -z $loopdev ] && sudo losetup -d $loopdev; exit 1' HUP TERM INT
else
    trap 'sudo kpartx -d $imgname; exit 1' HUP TERM INT
fi

echo "Creating partition $partname with base $partstart $partmul, size $partsize $partmul, and filesystem $partfs..."

# If this is a floppy image, we just need to format it. If it is a
# ISO9660 boot partition, we create an image named 'nnboot.img' and
# format it
# Finally, we create a loopback device and return that through the pipe
if [ "$parttype" = "floppy" ]
then
    # Set up loopback
    loopdev=$(sudo losetup -f)
    checkerr $? "unable to setup loopback"
    sudo losetup $loopdev $imgname
    checkerr $? "unable to setup loopback"
    # Format it
    sudo mkdosfs -F 12 $loopdev
    checkerr $? "unable to format partition"
    sudo losetup -d $loopdev
elif [ "$parttype" = "ISO9660" ]
then
    # If filesystem is ISO9660, then do nothing. Else, create 'nnboot.img'
    if [ "$partfs" = "ISO9660" ]
    then
        exit 0
    elif [ "$partfs" = "ext2" ]
    then
        # Not allowed...
        echo "$progname: ext2 not allowed on ISO9660 images"
        exit 1
    fi
    # Check emulation
    if ([ "$bootemu" = "noemu" ] && [ "$bootmode" != "efi" ]) || [ "$isbootpart" = "0" ]
    then
        echo "$progname: FAT partition not allowed on no emulation disks or non-bootable partitions on CD-ROMs"
        exit 1
    fi
    # Create disk image
    dd if=/dev/zero of=nnboot.img bs=${mulsize} count=${partsize}
    checkerr $? "unable to create nnboot.img"
    # Set up loopback
    loopdev=$(sudo losetup -f)
    checkerr $? "unable to setup loopback"
    sudo losetup $loopdev nnboot.img
    checkerr $? "unable to setup loopback"
    # Figure out FAT size
    if [ "$partfs" = "FAT12" ]
    then
        fatsize=12
    elif [ "$partfs" = "FAT16" ]
    then
        fatsize=16
    elif [ "$partfs" = "FAT32" ]
    then
        fatsize=32
    fi
    # Format it
    sudo mkdosfs -F $fatsize $loopdev
    checkerr $? "unable to format partition"
    sudo losetup -d $loopdev
else
    # Sanity checks
    if [ "$partfs" = "ISO9660" ] || [ "$partfs" = "FAT12" ]
    then
        echo "$progname: $partfs cannot be put on hard disk images"
    fi
    # Get parted file system name
    if [ "$partfs" = "FAT16" ]
    then
        partedfs="fat16"
    elif [ "$partfs" = "FAT32" ]
    then
        partedfs="fat32"
    elif [ "$partfs" = "ext2" ]
    then
        partedfs="ext2"
    fi
    partend=$(($partstart + $partsize))
    # Run parted to partition it
    if [ "$parttype" = "MBR" ]
    then
        sudo parted -s $imgname "unit $partmul mkpart primary $partedfs $partstart $partend"
        checkerr $? "unable to create partition"
        if [ "$isbootpart" = "1" ]
        then
            sudo parted -s $imgname "set $((partnum+1)) boot on"
            checkerr $? "unable to create partition"
        fi
    elif [ "$parttype" = "GPT" ]
    then
        sudo parted -s $imgname "unit $partmul mkpart $partname $partedfs $partstart $partend"
        checkerr $? "unable to create partition"
        if [ "$isbootpart" = "1" ]
        then
            if [ "$bootmode" = "efi" ] || [ "$bootmode" = "hybrid" ]
            then
                sudo parted -s $imgname "set $partnum esp on"
                checkerr $? "unable to create partition"
            elif [ "$bootmode" = "bios" ]
            then
                sudo parted -s $imgname "set $partnum boot on"
            else
                echo "$progname: invalid boot mode for GPT disk"
                exit 1
            fi
        fi
    fi
    # Add loopback
    loopdev=$(sudo kpartx -avs $imgname)
    sleep 1
    checkerr $? "unable to create loopback device"
    loopdev=/dev/mapper/$(echo "$loopdev" | awk -vline=$((partnum+1)) 'NR == line { print $3 }')
    # Format it
    if [ "$partfs" = "FAT32" ]
    then
        sudo mkdosfs -F 32 $loopdev
        checkerr $? "unable to format partition"
    elif [ "$partfs" = "FAT16" ]
    then
        sudo mkdosfs -F 16 $loopdev
        checkerr $? "unable to format partition"
    elif [ "$partfs" = "ext2" ]
    then
        sudo mke2fs $loopdev
        checkerr $? "unable to format partition"
    fi
    sleep 1
    sudo kpartx -d $imgname
    checkerr $? "unable to delete loopback device"
fi
