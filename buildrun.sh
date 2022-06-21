#!/bin/sh
# buildrun.sh - builds NexNix
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

package=
group=
target=i386-pc
conf=
debug=0
imgformat=mbr
toolchain=gnu
useninja=0
output="$PWD/output"
action=
imgaction=update
jobcount=1
image=
buildconf=

actions="all configure build image run dumpconf"

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

# Finds the argument to a given option
getoptarg()
{
    isdash=$(echo "$1" | awk '/^-/')
    if [ ! -z "$isdash" ]
    then
        panic "option $2 requires an argument"
    fi
    # Report it
    echo "$1"
}

# Action handlers
configure()
{
    # Set options
    if [ "$debug" = "1" ]
    then
        confargs="$confargs -debug"
    fi

    if [ "$useninja" = "1" ]
    then
        confargs="$confargs -ninja"
    fi

    # Run the configure script
    ./configure.sh $confargs -output $output -target $target -toolchain $toolchain \
                   -conf $conf -imgformat $imgformat -jobs $jobcount -buildconf $buildconf
    checkerr $? "unable to configure NexNix"
}

build()
{
    # Check for required arguments
    if [ -z "$package" ] && [ -z "$group" ]
    then
        panic "either package or group must be specified"
    fi
    # Run the configuration script
    olddir=$(pwd)
    # Check that the configuration exists
    if [ ! -d $output/conf/$target/$buildconf ]
    then
        panic "configuration $buildconf doesn't exist"
    fi
    cd $output/conf/$target/$buildconf && . $PWD/nexnix-conf.sh
    # Build it
    if [ ! -z $package ]
    then
        nnbuild -p $package all
        checkerr $? "unable to build NexNix"
    else
        nnbuild -g $group all
        checkerr $? "unable to build NexNix"
    fi
    cd $olddir
}

image()
{
    # Run the configuration script
    olddir=$(pwd)
    cd $output/conf/$target/$buildconf && . $PWD/nexnix-conf.sh
    nnimage -o $image $imgaction
    checkerr $? "unable to generate image for NexNix"
    cd $olddir
}

run()
{
    cd $output/conf/$target/$buildconf && . $PWD/nexnix-conf.sh && cd $olddir
    # Set the image arguments
    if [ "$imgformat" = "mbr" ]
    then
        $NNPROJECTROOT/scripts/run.sh $EMU_ARGS -disk $image $emuargs
    elif [ "$imgformat" = "iso9660" ]
    then
        $NNPROJECTROOT/scritps/run.sh $EMU_ARGS -cdrom $image -cdromboot $emuargs
    fi
}

dumpconf()
{
    # Check if configuration exists
    if [ ! -d $output/conf/$target/$buildconf ]
    then
        panic "configuration $target/$buildconf doesn't exist"
    fi
    echo $output/conf/$target/$buildconf/nexnix-conf.sh
}

# Start by parsing arguments
while [ $# -gt 0 ]
do
    case $1 in
    -help)
        cat <<HELPEND
$0 - wrapper script to configure, build, and run NexNix (or a subset)
Usage: $0 [-help] [-pkg package] [-group pkggroup] [-target target]
          [-conf conf] [-debug] [-imgformat format] [-toolchain toolchain]
          [-ninja] [-jobs jobs] [-imgaction action] [-image img]
          [-emulator-args args] [-buildconf conf] action

Valid arguments:
  -help
                        Shows this screen
  -pkg package
                        Specifies the package to build
                        Mutually exclusive with -group
  -group group
                        The package group to build
                        Mutually exclusdive with -pkg
  -target target
                        The target to build for
                        Run './configure.sh -archs' to see valid targets
                        Default: i386-pc
  -conf conf
                        The target configuration to build
                        Run './configure.sh -archs' to see valid configurations 
                        and defaults
  -debug
                        Build with debugging info and no optimizations
  -imgformat format
                        Specifies the image format
                        Valid arguments include "mbr" and "iso9660"
                        Run './configure.sh -archs' to see defaults
  -toolchain toolchain
                        Specifies the toolchain
                        Valid arguments include "llvm" and "gnu"
                        Default: "gnu"
  -ninja
                        Use the ninja build system
  -jobs jobs
                        Specifies the number of jobs that should be used 
                        to build NexNix
                        Default: 1
  -imgaction action
                        Specifies the aciton for nnimage to run
                        Valid arguments include "all", "create", "partition", 
                        and "update"
                        Default: "update"
  -image img
                        Specifies the image file to output
                        Default: configuration directory/nndisk.img 
                        (or nncdrom.iso in the case of ISO images)
  -emulator-args args
                        Specifies extra arguments to pass to run.sh
                        Ensure such arguments are quoted properly
                        By default, image path and appropiate boot device
                        are selected
  -buildconf conf
                        The configuration name for the build.
                        Defaults to the target configuration

Valid arguments for action include "all", "configure", "build", 
"image", "dumpconf", and "run". Default is "all"
HELPEND
        ;;
    -pkg)
        package=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -group)
        group=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -target)
        target=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -conf)
        conf=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -debug)
        debug=1
        shift
        ;;
    -imgformat)
        imgformat=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -toolchain)
        toolchain=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -ninja)
        useninja=1
        shift
        ;;
    -output)
        output=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -imgaction)
        imgaction=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -jobs)
        jobcount=$(getoptarg "$2" "$1")
        shift 2
        # Check if it is numeric
        isnum=$(echo $jobcount | awk '/[0-9]*/')
        if [ -z $isnum ]
        then
            panic "job count must be numeric"
        fi
        ;;
    -emulator-args)
        emuargs=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -buildconf)
        buildconf=$(getoptarg "$2" "$1")
        shift 2
        ;;
    *)
        isdash=$(echo "$1" | awk '/^-/')
        if [ ! -z "$isdash" ]
        then
            panic "invalid argument $1" 
        fi
        action=$1
        shift
        ;;
    esac
done

# Check that one fo package and group are specified
if [ ! -z "$package" ] && [ ! -z "$group" ]
then
    panic "package and group specification are mutually exclusive"
fi

# If no configuration was specified, then figure out the default
if [ -z "$conf" ]
then
    conf=$(./configure.sh -archs | awk -v target=$target '$2 == "configuration" && $4 == target { print $6 }')
fi

# Set buildconf, if need be
if [ -z "$buildconf" ]
then
    buildconf="$conf"
fi

# Set image path
if [ "$imgformat" = "mbr" ]
then
    image=$output/conf/$target/$buildconf/nnimage.img
elif [ "$imgformat" = "iso9660" ]
then
    image=$output/conf/$target/$buildconf/nncdrom.iso
fi

# Set action, if need be
if [ -z "$action" ]
then
    action=all
fi

# Run the action
if [ "$action" = "configure" ]
then
    configure
elif [ "$action" = "build" ]
then
    build
elif [ "$action" = "image" ]
then
    image
elif [ "$action" = "run" ]
then
    run
elif [ "$action" = "dumpconf" ]
then
    dumpconf
elif [ "$action" = "buildrun" ]
then
    build
    image
    run
elif [ "$action" = "all" ]
then
    configure
    build
    image
    run
else
    panic "invalid action $action specified"
fi
