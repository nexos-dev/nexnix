#! /bin/sh
# hwcopy-raspi3.sh - copies NexNix and firmware to RPi3
# Copyright 2023 - 2024 The NexNix Project
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
    echo "$0: error: $1"
    exit 1
}

# Checks return value and panics if an error occured
checkerr()
{
    # Check if return value is success or not
    if [ $1 -ne 0 ]
    then
        panic "$2" $3
    fi
}

# Finds the argument to a given option
getoptarg()
{
    isdash=$(echo "$1" | awk '/^-/')
    if [ ! -z "$isdash" ]
    then
        panic "option $2 requires an argument" $0
    fi
    # Report it
    echo "$1"
}

# Argument flags
device=
target=
conf=
nofw=0

while [ $# -gt 0 ]
do
    case $1 in
        -help)
            cat <<HELPEND
$(basename $0) - copies firmware and system to SD card for RPi3
Usage: $0 [-help] [-target target] [-conf conf] [-device device]
  -help
                Show this help menu
  -target
                Specifies target to copy from
  -conf
                Specifies configuration name to copy from
  -device
                Specifies device file to copy to
  -nofw
                Don't copy firmware or format

$(basename $0) automatically will download the appropriate firmware images.
As it currently stands, we are using the PFTF's EDK2 builds
HELPEND
            exit 0
            ;;
        -target)
            target=$(getoptarg "$2" "$1")
            shift 2
            ;;
        -conf)
            conf=$(getoptarg "$2" "$1")
            shift 2
            ;;
        -device)
            device=$(getoptarg "$2" "$1")
            shift 2
            ;;
        -nofw)
            nofw=1
            shift
            ;;
        *)
            panic "invalid argument $1"
            ;;
    esac
done

# Validate arguments
if [ -z "$target" ]
then
    panic "target not specified"
fi

if [ -z "$conf" ]
then
    panic "configuration not specified"
fi

if [ -z "$device" ]
then
    panic "device not specified"
fi

# Source in script
if [ ! -f output/conf/$target/$conf/nexnix-conf.sh ]
then
    panic "configuration $target_$conf doesn't exist"
fi

. output/conf/$target/$conf/nexnix-conf.sh

# Format device
if [ $nofw -eq 0 ]
then
    # Download PFTF archive
    olddir=$PWD
    mkdir -p hwcopy/fw/tarballs
    mkdir -p hwcopy/fw/rpi3
    if [ ! -f hwcopy/fw/tarballs/RPi3_UEFI_Firmware_v1.39.zip ]
    then
        cd hwcopy/fw/tarballs
        wget https://github.com/pftf/RPi3/releases/download/v1.39/RPi3_UEFI_Firmware_v1.39.zip
        checkerr $? "unable to dowbload RPi3 firmware"
        cd $olddir
        unzip hwcopy/fw/tarballs/RPi3_UEFI_Firmware_v1.39.zip -d hwcopy/fw/rpi3
        checkerr $? "unable to extract RPi3 firmware"
    fi
fi

# Check if this is MMC
[ $(echo $device | grep "mmc") ] && device=${device}p

# Copy out firmware
sudo umount ${device}1
sudo mount ${device}1 /mnt
if [ $nofw -eq 0 ]
then
    echo "Copying firmware..."
    sudo cp -r hwcopy/fw/rpi3/* /mnt/
fi
# Copy out NexNix files
echo "Copying NexNix..."
sudo mkdir -p /mnt/Programs
sudo mkdir -p /mnt/System
sudo cp -r output/conf/$target/$conf/sysroot/Programs/* /mnt/Programs/ > /dev/null 2> /dev/null
sudo cp -r output/conf/$target/$conf/sysroot/System/* /mnt/System/

# Copy nexboot.cfg
sudo cp /mnt/System/Core/Boot/nexboot.cfg /mnt/nexboot.cfg
# Copy kernel
sudo cp /mnt/System/Core/Boot/nexke /mnt/nexke
# Create BOOTAA64.EFI
sudo mkdir -p /mnt/EFI/BOOT
sudo cp /mnt/System/Core/Boot/nexboot.efi /mnt/EFI/BOOT/BOOTAA64.EFI
# Remove links
sudo rm -rf /mnt/Programs/Index
sudo umount /mnt
