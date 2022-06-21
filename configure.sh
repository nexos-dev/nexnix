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
    else
        sudo $@
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
        runroot apt install $pkgs
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
depcheckfail=0
useninja=0
genconf=1
installpkgs=0
usesu=0
hostos=
pkgs=

# Target list
targets="i386-pc"
# Valid image types
imagetypes="mbr iso9660"
# Target configuration list
confs_i386_pc="isa"

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

Valid arguments:
  -help
                        Show this help menu
  -debug
                        Include debugging info, asserts, etc.
  -archs
                        List supported targets and configurations
  -target target
                        Build the system for the specified target. 
                        Specify -l to list architectures
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
  -imgformat imagetype
                        Specifies the type of the disk image
                        Valid arguments include  "mbr" and "iso9660"
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
HELPEND
        exit 0
        ;;
    -archs)
        # Print out all targets in target table
        echo "Valid targets:"
        echo $targets
        echo "Valid configurations for i386-pc:"
        echo $confs_i386_pc
        echo "Default configuration for i386-pc is: isa"
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
    *)
        # Invalid option
        panic "invalid option $1" $0
    esac
done

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
        elif [ ! -z $(cat /etc/*release | grep -i "fedora") ]
        then
            echo "Detected OS: Fedora"
            hostos="fedora"
        elif [ ! -z $(cat /etc/*release | grep -i "arch|manjaro") ]
        then
            echo "Detected OS: Arch Linux"
            hostos="arch"
        else
            echo "Detected OS: Unknown"
            installpkgs=0
        fi
    fi
fi

################################
# Program check
################################

findprog "cmake"
if [ $useninja -eq 1  ]
then
    findprog "ninja"
else
    findprog "gmake"
fi
findprog "wget"
findprog "tar"
findprog "git"
findprog "gcc" "clang"
findprog "g++" "clang++"
if [ "$toolchain" = "gnu" ]
then
    findprog "gmake"
fi
findprog "pkg-config"
findlib "uuid"
findlib "guestfs"
# Check if the check succeeded
if [ $depcheckfail -eq 1 ]
then
    panic "missing dependency" $0
fi

# Install depndencies
if [ ! -z $pkgs ]
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
            tarconf="isa"
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

        # Set the parameters of this configuration
        if [ "$tarconf" = "isa" ]
        then
            if [ -z "$imagetype" ]
            then
                imagetype=mbr
            fi
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
    
    ############################
    # nnbuild bootstraping
    ############################

    if [ $useninja -eq 1 ]
    then
        cmakeargs="-G Ninja"
        cmakegen=ninja
    else
        cmakegen=gmake
        makeargs="--no-print-directory"
    fi

    # Download libchardet
    if [ ! -f $olddir/source/external/libraries/libchardet_done ] || [ "$NNTOOLS_REGET_CHARDET" = "1" ]
    then
        rm -rf $olddir/source/external/libraries/libchardet
        git clone https://github.com/nexos-dev/libchardet.git \
                  $olddir/source/external/libraries/libchardet
        touch $olddir/source/external/libraries/libchardet_done
    fi
    # Build libchardet
    if ([ ! -f $output/tools/lib/libchardet.a ] && [ ! -f $output/tools/lib64/libchardet.a ]) \
       || [ "$NNTOOLS_REBUILD_CHARDET" = "1" ]
    then
        chardetbuild="$output/build/tools/chardet-build/$cmakegen"
        mkdir -p $chardetbuild && cd $chardetbuild
        cmake $olddir/source/external/libraries/libchardet -DCMAKE_INSTALL_PREFIX="$output/tools" $cmakeargs
        checkerr $? "unable to configure libchardet" $0
        $cmakegen -j $jobcount
        checkerr $? "unable to build libchardet" $0
        $cmakegen install -j $jobcount
        checkerr $? "unable to build libchardet" $0
    fi
    # Download libnex
    if [ ! -f $olddir/source/external/libraries/libnex_done ] || [ "$NNTOOLS_REGET_LIBNEX" = "1" ]
    then
        rm -rf $olddir/external/source/libraries/libnex
        git clone https://github.com/nexos-dev/libnex.git \
                  $olddir/source/external/libraries/libnex
        touch $olddir/source/external/libraries/libnex_done
    fi
    # Build libnex
    if [ ! -f $output/tools/lib/libnex.a -a ! -f $output/tools/lib64/libnex.a ] || 
       [ "$NNTOOLS_REBUILD_LIBNEX" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            libnex_cmakeargs="$cmakeargs -DLIBNEX_ENABLE_TESTS=ON"
        else
            libnex_cmakeargs="$cmakeargs"
        fi
        libnex_builddir="$output/build/tools/libnex-build"
        mkdir -p $libnex_builddir/$cmakegen
        cd $libnex_builddir/$cmakegen
        cmake $olddir/source/external/libraries/libnex -DCMAKE_INSTALL_PREFIX="$output/tools" $libnex_cmakeargs
        checkerr $? "unable to configure libnex" $0
        $cmakegen -j $jobcount $makeargs
        checkerr $? "unable to build libnex" $0
        $cmakegen -j $jobcount $makeargs install
        checkerr $? "unable to install libnex" $0
        # Run tests if requested
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            ctest -V
            checkerr $? "test suite failed" $0
        fi
    fi
    # Build host tools
    if [ ! -f "$output/tools/bin/nnimage" ] || [ "$NNTOOLS_REBUILD_TOOLS" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            tools_cmakeargs="$cmakeargs -DTOOLS_ENABLE_TESTS=ON"
        else
            tools_cmakeargs="$cmakeargs"
        fi
        builddir=$output/build/tools/tools-build
        mkdir -p $builddir/$cmakegen
        cd $builddir/$cmakegen
        # So libuuid is found
        export PKG_CONFIG_PATH="$output/tools/lib/pkgconfig"
        cmake $olddir/source/hosttools -DCMAKE_INSTALL_PREFIX="$output/tools" $tools_cmakeargs
        checkerr $? "unable to build host tools" $0
        $cmakegen -j$jobcount $makeargs
        checkerr $? "unable to build host tools" $0
        $cmakegen install -j$jobcount $makeargs
        checkerr $? "unable to build host tools" $0
        # Run tests if requested
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            ctest -V
            checkerr $? "test suite failed" $0
        fi
    fi

    #############################
    # Build system configuration
    #############################

    # Now we need to configure this target's build directory
    if [ $genconf -eq 1 ]
    then
        mkdir -p $output/conf/$target/$conf && cd $output/conf/$target/$conf
        mkdir -p $prefix
        # Setup target configuration script
        echo "export PATH=\"\$PATH:$output/tools/bin\"" > nexnix-conf.sh
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
        echo "export NNUEFIARCH=$buildfw" >> nexnix-conf.sh
        echo "export NNTARGETCONF=$tarconf" >> nexnix-conf.sh
        echo "export NNUSENINJA=$useninja" >> nexnix-conf.sh
        echo "export NNSCRIPTROOT=\"$olddir/scripts\"/" >> nexnix-conf.sh
        echo "export NNMOUNTDIR=\"$output/conf/$target/$conf/fsdir\"/" >> nexnix-conf.sh
    fi
    # Reset target configuration
    tarconf=
done

cd $olddir
