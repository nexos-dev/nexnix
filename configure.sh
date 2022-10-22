#!/bin/sh
# configure.sh - configure NexNix to build
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

####################################
# Start shell test
####################################

printf "Checking shell..."

# Check for !
if eval '! true > /dev/null 2>&1'
then
    echo "$0: error: shell doesn't work"
    exit 1
fi

# Check for functions
if ! eval 'x() { : ; } > /dev/null 2>&1'
then
    echo "$0: error: shell doesn't work"
    exit 1
fi

# Check for $()
if ! eval 'v=$(echo "abc") > /dev/null 2>&1'
then
    echo "$0: error: shell doesn't work"
    exit 1
fi

echo "done"
# The shell works. Let's start configuring now

###########################################
# Helper functions
###########################################

# Panics with a message and then exits
panic()
{
    # Check if the name should be printed out
    if [ ! -z "$2" ]
    then
        echo "$2: error: $1"
    else
        echo "error: $1"
    fi
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

# Runs a program as root
runroot()
{
    # Check of program should be run with su or sudo
    if [ "$usesu" = "1" ]
    then
        su -c "$@"
        checkerr $? "unable to install package" $0
    else
        sudo $@
        checkerr $? "unable to install package" $0
    fi
}

# Finds a library
findlib()
{
    # Figure out include directories possible
    # We include /usr/include, /usr/pkg/include, and /usr/local/include
    incdirs="/usr/include /usr/pkg/include /usr/local/include"
    printf "Checking for $1..."
    for dir in $incdirs
    do
        # Figure out package
        if [ "$1" = "uuid" ]
        then
            if [ -f ${dir}/uuid/uuid.h ]
            then
                echo "found"
                return
            fi
        elif [ "$1" = "guestfs" ]
        then
            if [ -f ${dir}/guestfs.h ]
            then
                echo "found"
                return
            fi
        fi
    done
    echo "not found"
    if [ "$installpkgs" = "1" ]
    then
        if [ "$1" = "uuid" ]
        then
            if [ "$hostos" = "debian" ]
            then
                pkg=uuid-dev
            elif [ "$hostos" = "fedora" ]
            then
                pkg=libuuid-devel
            elif [ "$hostos" = "arch" ]
            then
                pkg=libuuid-devel
            fi
        elif [ "$1" = "guestfs" ]
        then
            if [ "$hostos" = "debian" ]
            then
                pkg=libguestfs-dev
            elif [ "$hostos" = "fedora" ]
            then
                pkg=libguestfs-devel
            elif [ "$hostos" = "arch" ]
            then
                pkg=libguestfs-dev
            fi
        fi
        pkgs="$pkgs $pkg"
    else
        depcheckfail=1
    fi
}

# Finds a program in the PATH. Panic if not found
findprog()
{
    # Save PATH, also making it able for us to loop through with for
    pathtemp=$PATH
    pathtemp=$(echo "$pathtemp" | sed 's/:/ /g')
    # Go through each directory, trying to see if the program exists
    for prog in $@
    do
        printf "Checking for $prog..."
        for dir in $pathtemp
        do
            if [ -f ${dir}/${prog} ]
            then
                echo ${dir}/${prog}
                return
            fi
        done
        echo "not found"
    done
    # Check if it should be installed
    if [ "$installpkgs" = "1" ]
    then
        if [ "$1" = "ninja" ]
        then
            if [ "$hostos" = "arch" ]
            then
                pkg=ninja
            else
                pkg=ninja-build
            fi
        elif [ "$1" = "gcc" ]
        then
            pkg=$1
        elif [ "$1" = "g++" ]
        then
            pkg=$1
        elif [ "$1"  = "make" ]
        then
            pkg=$1
        elif [ "$1" = "cmake" ]
        then
            pkg=$1
        elif [ "$1" = "pkg-config" ]
        then
            pkg=$1
        elif [ "$1" = "git" ]
        then
            pkg=$1
        elif [ "$1" = "tar" ]
        then
            pkg=$1
        elif [ "$1" = "wget" ]
        then
            pkg=$1
        elif [ "$1" = "ld" ]
        then
            pkg=binutils
        elif [ "$1" = "lld" ]
        then
            pkg="llvm lld"
        elif [ "$1" = "nasm" ]
        then
            pkg="nasm"
        elif [ "$1" = "tex" ]
        then
            pkg="texlive-binaries texlive-latex-extra"
        elif [ "$1" = "xz" ]
        then
            if [ "$hostos" = "debain" ]
            then
                pkg="xz-utils"
            elif [ "$hostos" = "fedora" ]
            then
                pkg="xz"
            elif [ "$hostos" = "arch" ]
            then
                pkg="xz"
            fi
        elif [ "$1" = "python" ]
        then
            pkg=$1
        elif [ "$1" = "bash" ]
        then
            pkg=$1
        elif [ "$1" = "iasl" ]
        then
            pkg="acpica-tools"
        elif [ "$1" = "nasm" ]
        then
            pkg="nasm"
        fi
        pkgs="$pkgs $pkg"
    else
        depcheckfail=1
    fi
}

# Installs packages
installpkgs()
{
    if [ "$installpkgs" = "0" ]
    then
        return
    fi
    if [ "$hostos" = "fedora" ]
    then
        runroot dnf install $pkgs
    elif [ "$hostos" = "debian" ]
    then
        runroot apt install -y $pkgs
    elif [ "$hostos" = "arch" ]
    then
        runroot pacman -S $pkgs
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

####################################
# Parse arguments
####################################

# Argument values
output="$PWD/output"
tarearly=i386-pc
dodebug=1
jobcount=1
compiler=llvm
debug=0
conf=
genconf=1
tarconf=
commonarch=
arch=
board=
imagetype=
imgbootemu=
imgbootmode=
imguniversal=0
depcheckfail=0
useninja=0
genconf=1
installpkgs=0
usesu=0
hostos=
pkgs=
fwtype=

# System configuration options
loglevel=4
graphicsmode=text

# Target specific features
targetismp=
commonarch=

# i386 options
ispae=

# Target list
targets="i386-pc"
# Valid image types
imagetypes="mbr gpt iso9660"
# Target configuration list
confs_i386_pc="isa pnp mp acpi-up acpi"

# Loop through every argument
while [ $# -gt 0 ]
do
    case $1 in
    -help)
        cat <<HELPEND
$(basename $0) - configures the NexNix build system
Usage - $0 [-help] [-archs] [-debug] [-target target] [-toolchain llvm|gnu] [-jobs jobcount] 
           [-output destdir] [-buildconf conf] [-prefix prefix]
           [-imgformat image_type] [-conf conf] [-installpkgs] [-su]

Help options:
  -help
                        Show this help menu
  -archs
                        List supported targets and configurations
Build system configuration options:
  -target target
                        Build the system for the specified target. 
                        Run with options -archs to list targets
  -toolchain toolchain
                        Specifies if the "llvm" or "gnu" toolchain should be used
                        Default: llvm
  -buildconf conf
                        Specifies a name for the build configuration
  -jobs jobcount
                        Specifies job count to use
  -output destdir
                        Specfies directory to output build files to
  -prefix prefix
                        Specifies prefix directory
  -fwtype type
                        Specifies firmware type
                        Can be "bios" or "efi"
  -conf conf
                        Specifies the configuration for the target
  -ninja
                        Use the ninja build system instead of GNU make
  -nogen
                        Don't generate a configuration
  -installpkgs
                        Automatically install missing dependencies
                        Note that this requires root privileges
  -su
                        Use su(1) to run commands as root
                        By default, sudo(1) is used
Image output options:
  -imgformat imagetype
                        Specifies the type of the disk image
                        Valid arguments include  "mbr", "gpt", and "iso9660"
  -imgbootmode bootmode
                        Specifies the boot mode to use on the image
                        Valid arguments include "noboot", "isofloppy",
                        "bios", "efi", and "hybrid"
  -imgbootemu bootemu
                        Specifies boot emulation on iso9660 bios or hybrid images
                        Valid arguments include "hdd", "fdd", or "noemu"
System configuration options:
  -debug
                        Include debugging info, asserts, etc.
  -loglevel level
                        Set NexNix's log level to specfied level.
                        Can be "none", "errors", "warnings", "info", 
                        or "verbose".
  -graphicsmode mode
                        Specifies if graphical output mode
                        Can be "gui", "text", or "headless"
Options for target i386:
These options should be passed after -target option
  -pae val
                        Specifies if PAE should be used by OS
                        Value can be "on" or "off", defaults to "on"
                        for configurations acpi and acpi-up, "off"
                        for everything else
HELPEND
        exit 0
        ;;
    -archs)
        # Print out all targets in target table
        echo "Valid targets:"
        echo $targets
        echo "Valid configurations for i386-pc:"
        echo $confs_i386_pc
        echo "Default configuration for i386-pc is: acpi"
        echo "Default image type for i386-pc is: mbr"
        exit 0
        ;;
    -debug)
        # Enable debug mode
        debug=1
        shift
        ;;
    -conf)
        # Grab the argument
        tarconf=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -imgformat)
        # Get the argument
        imagetype=$(getoptarg "$2" "$1")
        shift 2
        # Validate it
        imgfound=0
        for imgtype in $imagetypes
        do
            if [ "$imgtype" = "$imagetype" ]
            then
                imgfound=1
                break
            fi
        done
        if [ $imgfound -eq 0 ]
        then
            panic "image type \"$imagetype\" invalid" $0
        fi
        ;;
    -imgbootemu)
        imgbootemu=$(getoptarg "$2" "$1")
        shift 2
        bootemufound=0
        for bootemu in hdd fdd noemu
        do
            if [ "$imgbootemu" = "$bootemu" ]
            then
                bootemufound=1
                break
            fi
        done
        if [ $bootemufound -eq 0 ]
        then
            panic "boot emulation \"$imgbootemu\" invalid" $0
        fi
        ;;
    -imgbootmode)
        imgbootmode=$(getoptarg "$2" "$1")
        shift 2
        bootmodefound=0
        for bootmode in isofloppy bios noboot efi hybrid
        do
            if [ "$imgbootmode" = "$bootmode" ]
            then
                bootmodefound=1
                break
            fi
        done
        if [ $bootmodefound -eq 0 ]
        then
            panic "boot mode \"$imgbootmode\" is invalid" $0
        fi
        ;;
    -fwtype)
        fwtype=$(getoptarg "$2" "$1")
        shift 2
        fwfound=0
        for fw in bios efi
        do
            if [ "$fwtype" = "$fw" ]
            then
                fwfound=1
                break
            fi
        done
        if [ $fwfound -eq 0 ]
        then
            panic "firmware type \"$fwtype\" is invalid" $0
        fi
        ;;
    -target)
        # Find the argument
        tarearly=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        ;;
    -toolchain)
        # Find the argument
        compiler=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        ;;
    -jobs)
        # Find the argument
        jobcount=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        # Check if it is numeric
        isnum=$(echo $jobcount | awk '/[0-9]*/')
        if [ -z $isnum ]
        then
            panic "job count must be numeric"
        fi
        ;;
    -output)
        # Find the argument
        output=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        # Check to ensure that it is abosolute
        start=$(echo "$output" | awk '/^\//')
        if [ -z "$start" ]
        then
            panic "output directory must be absolute" $0
        fi
        ;;
    -prefix)
        # Find the argument
        prefix=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        ;;
    -buildconf)
        # Find the argument
        conf=$(getoptarg "$2" "$1")
        # Shift accordingly
        shift 2
        ;;
    -ninja)
        useninja=1
        shift
        ;;
    -nogen)
        genconf=0
        shift
        ;;
    -installpkgs)
        installpkgs=1
        shift
        ;;
    -su)
        usesu=1
        shift
        ;;
    -loglevel)
        # Get the argument
        loglevelname=$(getoptarg "$2" "$1")
        # Convert to numeric form
        if [ "$loglevelname" = "none" ]
        then
            loglevel=0
        elif [ "$loglevelname" = "errors" ]
        then
            loglevel=1
        elif [ "$loglevelname" = "warnings" ]
        then
            loglevel=2
        elif [ "$loglevelname" = "info" ]
        then
            loglevel=3
        elif [ "$loglevelname" = "verbose" ]
        then
            loglevel=4
        else
            panic "invalid loglevel $loglevelname" $0
        fi
        shift 2
        ;;
    -graphicsmode)
        graphicsmode=$(getoptarg "$2" "$1")
        modefound=0
        for mode in gui text none
        do
            if [ "$graphicsmode" = "$mode" ]
            then
                modefound=1
                break
            fi
        done
        if [ $modefound -eq 0 ]
        then
            panic "invalid graphics mode specified" $0
        fi
        shift 2
        ;;
    -pae)
        ispae=$(getoptarg "$2" "$1")
        valvalid=0
        for val in on off
        do
            if [ "$val" = "$ispae" ]
            then
                valvalid=1
                break
            fi
        done
        if [ $valvalid -eq 0 ]
        then
            panic "invalid PAE mode specified" $0
        fi
        shift 2
        ;;
    *)
        # Invalid option
        panic "invalid option $1" $0
    esac
done

# Check for grep
printf "Checking for grep..."
if [ -z $(command -v grep) ]
then
    echo "not found"
    panic "grep not found" $0
fi
echo "found"

# Detect host OS for package installation
if [ "$installpkgs" = "1" ]
then
    if [ "$(uname)" = "Linux" ]
    then
        # Detect distro
        # This is based off of Tilck's build system detection:
        # https://github.com/vvaltchev/tilck/blob/9873e764402519c30a993ec5c2761e4332f76313/scripts/tc/pkgs/install_pkgs
        if [ -f /etc/os-release ] && [ ! -z $(cat /etc/os-release | grep -i "debian") ]
        then
            echo "Detected OS: Debian"
            hostos="debian"
            if [ "$NNTOOLS_NO_WARNING" != "1" ]
            then
                # Warn Ubuntu users because kernel is not world readable in Ubuntu,
                # which makes image generation field
                echo "WARNING Ubuntu users: ensure your kernel is world readable, else"
                echo "image generation will fail! Please run:"
                echo "    sudo chmod +r /boot/vmlinuz-*"
                sleep 1
            fi
        elif [ ! -z $(cat /etc/*release | grep -i "fedora") ]
        then
            echo "Detected OS: Fedora"
            hostos="fedora"
        elif [ ! -z $(cat /etc/*release | grep -Ei "arch|manjaro") ]
        then
            echo "Detected OS: Arch Linux"
            hostos="arch"
        else
            echo "Detected OS: Unknown"
            installpkgs=0
        fi
    else
        echo "Detected OS: Unknown"
        echo "WARNING: This system has not been tested with NexNix's build system."
        echo "There may be errors during the build"
        echo "Also, package autoinstallation is DISABLED"
        sleep 1
        installpkgs=0
    fi
fi

################################
# Program check
################################

findprog "cmake"
if [ $useninja -eq 1  ]
then
    findprog "ninja"
fi
findprog "wget"
findprog "tar"
findprog "git"
findprog "cc" "gcc" "clang"
findprog "c++" "g++" "clang++"
findprog "ld" "lld"
findprog "ar"
if [ "$toolchain" = "gnu" ] || [ $useninja -eq 0 ]
then
    findprog "gmake"
fi
findprog "xz"
findprog "python"
findprog "iasl"
findprog "bash"
findprog "nasm"
findlib "guestfs"
findlib "uuid"
# Check if the check succeeded
if [ $depcheckfail -eq 1 ]
then
    panic "missing dependency" $0
fi

# Install depndencies
if [ ! -z "$pkgs" ]
then
    installpkgs
fi

# Check for all in target
if [ "$tarearly" = "all" ]
then
    tarearly=$targets
fi

olddir=$PWD

# Go through every target, building its utilites
for target in $tarearly
do
    # Grab the arch for building
    arch=$(echo "$target" | awk -F'-' '{ print $1 }')
    board=$(echo "$target" | awk -F'-' '{ print $2 }')
    
    # Check the architecture
    printf "Checking target architecture..."
    # Try to find it
    targetfound=0
    for tar in $targets
    do
        if [ "$tar" = "$target" ]
        then
            targetfound=1
            break
        fi
    done

    # Check if it was found
    if [ $targetfound -ne 1 ]
    then
        echo "unknown"
        panic "target architecture $target unsupported. 
Run $0 -l to see supported targets"
    fi
    echo "$target"
    
    # Validate the target configuration
    if [ "$target" = "i386-pc" ]
    then
        commonarch="x86"
        if [ -z "$tarconf" ]
        then
            tarconf="acpi"
        else
            conffound=0
            for tconf in $confs_i386_pc
            do
                if [ "$tconf" = "$tarconf" ]
                then
                    conffound=1
                    break
                fi
            done
            if [ $conffound -eq 0 ]
            then
                panic "configuration \"$tarconf\" invalid for target \"$target\"" $0
            fi
        fi

        # Validate options
        if [ ! -z "$ispae" ]
        then
            if [ "$ispae" = "on" ]
            then
                ispae=1
            else
                ispae=0
            fi
        fi

        # Set the parameters of this configuration
        if [ "$tarconf" = "acpi" ] || [ "$tarconf" = "mp" ]
        then
            # Ensure environment is valid
            if [ "$imgbootmode" = "none" ]
            then
                panic "boot mode \"none\" not valid for i386-pc configuration $tarconf"
            fi
            [ -z "$imagetype" ] && imagetype="mbr"
            [ -z "$imgbootmode" ] && imgbootmode="bios"
            [ -z "$imgbootemu" ] && imgbootemu="fdd"
            [ -z "$fwtype" ] && fwtype="bios"
            if [ "$tarconf" = "acpi" ]
            then
                [ -z "$ispae" ] && ispae=1
            else
                [ -z "$ispae" ] && ispae=0
            fi
            targetismp=1
        elif [ "$tarconf" = "acpi-up" ] || [ "$tarconf" = "pnp" ] || \
             [ "$tarconf" = "isa" ]
        then
            # Ensure environment is valid
            if [ "$imgbootmode" = "none" ]
            then
                panic "boot mode \"none\" not valid for i386-pc configuration $tarconf"
            fi
            [ -z "$imagetype" ] && imagetype="mbr"
            [ -z "$imgbootmode" ] && imgbootmode="bios"
            [ -z "$imgbootemu" ] && imgbootemu="fdd"
            [ -z "$fwtype" ] && fwtype="bios"
            if [ "$tarconf" = "acpi-up" ]
            then
                [ -z "$ispae" ] && ispae=1
            else
                [ -z "$ispae" ] && ispae=0
            fi
            targetismp=0
        fi
    fi
    # Setup build configuration name
    if [ -z "$conf" ]
    then
        conf=${tarconf}
    fi

    # Check if the prefix needs to be set to its default
    if [ -z "$prefix" ]
    then
        prefix="$output/conf/$target/$conf/sysroot"
    fi
    
    #############################
    # Host tools bootstraping
    #############################
    export NNSOURCEROOT=$olddir/source
    export NNBUILDROOT=$output
    export NNJOBCOUNT=$jobcount
    export NNUSENINJA=$useninja
    $olddir/scripts/hostbootstrap.sh hostlibs
    checkerr $? "unable to bootstrap host tools" $0
    $olddir/scripts/hostbootstrap.sh hosttools
    checkerr $? "unable to bootstrap host tools" $0

    #############################
    # Build system configuration
    #############################

    # Now we need to configure this target's build directory
    if [ $genconf -eq 1 ]
    then
        mkdir -p $output/conf/$target/$conf && cd $output/conf/$target/$conf
        mkdir -p $prefix
        # Setup target configuration script
        echo "export PATH=\"$output/tools/bin:$output/tools/$compiler/bin:\$PATH\"" > nexnix-conf.sh
        echo "export NNBUILDROOT=\"$output\"" >> nexnix-conf.sh
        echo "export NNTARGET=\"$target\"" >> nexnix-conf.sh
        echo "export NNARCH=\"$arch\"" >> nexnix-conf.sh
        echo "export NNBOARD=\"$board\"" >> nexnix-conf.sh
        echo "export NNDESTDIR=\"$prefix\"" >> nexnix-conf.sh
        echo "export NNJOBCOUNT=\"$jobcount\"" >> nexnix-conf.sh
        echo "export NNCONFROOT=\"$output/conf/$target/$conf\"" >> nexnix-conf.sh
        echo "export NNPROJECTROOT=\"$olddir\"" >> nexnix-conf.sh
        echo "export NNSOURCEROOT=\"$olddir/source\"" >> nexnix-conf.sh
        echo "export NNEXTSOURCEROOT=\"$olddir/source/external\"" >> nexnix-conf.sh
        echo "export NNOBJDIR=\"$output/build/${target}-${conf}_objdir\"" >> nexnix-conf.sh
        echo "export NNTOOLCHAIN=\"$compiler\"" >> nexnix-conf.sh
        echo "export NNDEBUG=\"$debug\"" >> nexnix-conf.sh
        echo "export NNTOOLCHAINPATH=\"$output/tools/$compiler/bin\"" >> nexnix-conf.sh
        echo "export NNCOMMONARCH=\"$commonarch\"" >> nexnix-conf.sh
        echo "export NNTARGETCONF=$tarconf" >> nexnix-conf.sh
        echo "export NNUSENINJA=$useninja" >> nexnix-conf.sh
        echo "export NNSCRIPTROOT=\"$olddir/scripts/\"" >> nexnix-conf.sh
        echo "export NNMOUNTDIR=\"$output/conf/$target/$conf/fsdir/\"" >> nexnix-conf.sh
        echo "export NNTARGETISMP=$targetismp" >> nexnix-conf.sh
        echo "export NNFIRMWARE=\"$fwtype\"" >> nexnix-conf.sh
        echo "export NNPKGROOT=\"$olddir/scripts/packages\"" >> nexnix-conf.sh
        if [ "$imagetype" = "iso9660" ]
        then
            echo "export NNIMGBOOTEMU=\"$imgbootemu\"" >> nexnix-conf.sh
        fi
        echo "export NNIMGBOOTMODE=\"$imgbootmode\"" >> nexnix-conf.sh
        echo "export NNIMGTYPE=\"$imagetype\"" >> nexnix-conf.sh
        echo "export NNLOGLEVEL=$loglevel" >> nexnix-conf.sh
        echo "export NNGRAPHICSMODE=\"$graphicsmode\"" >> nexnix-conf.sh
        echo "export NNBOOTIMG=\"$output/conf/$target/$conf/nnboot.img\"" >> nexnix-conf.sh
        echo "export NNALTBOOTIMG=\"$output/conf/$target/$conf/nnboot2.img\"" >> nexnix-conf.sh
        echo "export NNTARGETCONF=$tarconf" >> nexnix-conf.sh
        if [ "$arch" = "i386" ]
        then
            echo "export NNISPAE=$ispae" >> nexnix-conf.sh
        fi
        # Link nnbuild.conf to configuration directory
        ln -sf $olddir/scripts/packages/nnbuild.conf $output/conf/$target/$conf/nnbuild.conf
    fi
    # Check if libguestfs image needs to be decompressed
    if [ ! -f $olddir/scripts/guestfs_root.img ]
    then
        echo "Decompressing libguestfs guest image..."
        xz -dk $olddir/scripts/guestfs_root.img.xz
    fi
    # Reset target configuration
    tarconf=
    echo "Output target configuration $conf"
done

cd $olddir
