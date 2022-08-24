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
target=
conf=
debug=0
imgformat=
toolchain=gnu
useninja=0
output="$PWD/output"
action=
imgaction=all
imgactiondef=1
jobcount=1
image=
buildconf=
confopts=
buildonly=0

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
    # Run the configure script
    ./configure.sh -installpkgs $confopts
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
        if [ "$buildonly" = "0" ]
        then
            nnbuild -p $package all
            checkerr $? "unable to build NexNix"
        elif [ "$action" = "confbuild" ]
        then
            nnbuild -p $package confbuild
        else
            nnbuild -p $package build
            checkerr $? "unable to build NexNix"
        fi
    else
        if [ "$buildonly" = "0" ]
        then
            nnbuild -g $group all
            checkerr $? "unable to build NexNix"
        elif [ "$action" = "confbuild" ]
        then
            nnbuild -g $group confbuild
        else
            nnbuild -g $group build
            checkerr $? "unable to build NexNix"
        fi
    fi
    cd $olddir
}

image()
{
    if [ "$imgactiondef" = "1" ] && [ "$action" = "buildrun" ]
    then
        imgaction="update"
    fi
    # Set image
    if [ "$NNIMGTYPE" = "mbr" ] || [ "$NNIMGTYPE" = "gpt" ]
    then
        image=$NNCONFROOT/nndisk.img
    elif [ "$NNIMGBOOTMODE" = "isofloppy" ]
    then
        image=$NNCONFROOT/nndisk.flp
    else
        image=$NNCONFROOT/nncdrom.iso
    fi
    # Run the configuration script
    olddir=$(pwd)
    cd $output/conf/$target/$buildconf && . $PWD/nexnix-conf.sh
    nnimage -l $NNCONFROOT/nnimage-list.lst $imgaction
    checkerr $? "unable to generate image for NexNix"
    cd $olddir
}

run()
{
    cd $output/conf/$target/$buildconf && . $PWD/nexnix-conf.sh
    # Set the image arguments
    imgformat=$NNIMGTYPE
    # Set image
    if [ "$NNIMGTYPE" = "mbr" ] || [ "$NNIMGTYPE" = "gpt" ]
    then
        image=$NNCONFROOT/nndisk.img
    elif [ "$NNIMGBOOTMODE" = "isofloppy" ]
    then
        image=$NNCONFROOT/nndisk.flp
    else
        image=$NNCONFROOT/nncdrom.iso
    fi
    if [ "$imgformat" = "mbr" ] || [ "$imgformat" = "gpt" ]
    then
        $NNPROJECTROOT/scripts/run.sh $EMU_ARGS -disk $image -fw $NNFIRMWARE $emuargs
    elif [ "$imgformat" = "iso9660" ]
    then
        if [ "$NNIMGBOOTMODE" = "isofloppy" ]
        then
            $NNPROJECTROOT/scripts/run.sh $EMU_ARGS -cdrom $NNCONFROOT/nncdrom.iso \
                            -floppy $image -floppyboot \
                            -fw $NNFIRMWARE $emuargs
        else
            $NNPROJECTROOT/scripts/run.sh $EMU_ARGS -cdrom $image -cdromboot \
                            -fw $NNFIRMWARE $emuargs
        fi
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

# Get action
if [ "$1" = "" ]
then
    panic "action not specified"
fi
isdash=$(echo "$1" | awk '/^-/')
if [ ! -z "$isdash" ] && [ "$1" != "-help" ]
then
    panic "invalid action \"$1\"" 
fi
if [ "$1" != "-help" ]
then
    action=$1
    shift
fi

# Start by parsing arguments
while [ $# -gt 0 ]
do
    case $1 in
    -help)
        cat <<HELPEND
$0 - wrapper script to configure, build, and run NexNix (or a subset)
Usage: $0 action [-help] [-pkg package] [-group pkggroup] [-imgaction action]
                 [-image img] [-emulator-args args]

Valid arguments:
  -help
                        Shows this screen
  -pkg package
                        Specifies the package to build
                        Mutually exclusive with -group
  -group group
                        The package group to build
                        Mutually exclusdive with -pkg
  -imgaction action
                        Specifies the aciton for nnimage to run
                        Valid arguments include "all", "create", "partition", 
                        and "update"
                        Default: "all" for most actions, "update" for buildrun
  -image img
                        Specifies the image file to output
                        Default: configuration directory/nndisk.img 
                        (or nncdrom.iso in the case of ISO images)
  -buildonly
                        Skip nnbuild download step
                        This option is useful for incremental builds
  -emulator-args args
                        Specifies extra arguments to pass to run.sh
                        Ensure such arguments are quoted properly
                        By default, image path and appropiate boot device
                        are selected
Run './configure.sh -help' to see additional options.
Any option passable to configure.sh is passable to buildrun.sh
Note that -installpkgs is always passed to configure.sh
Valid arguments for action include "all", "configure", "confbuild", "build", 
"image", "dumpconf", and "run". Default is "all"
HELPEND
        exit 0
        ;;
    -pkg)
        package=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -group)
        group=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -imgaction)
        imgaction=$(getoptarg "$2" "$1")
        imgactiondef=0
        shift 2
        ;;
    -emulator-args)
        emuargs=$(getoptarg "$2" "$1")
        shift 2
        ;;
    -buildonly)
        buildonly=1
        shift 1
        ;;
    -target)
        # Hook into -target option
        target=$(getoptarg "$2" "$1")
        confopts="$confopts $1 $2"
        shift 2
        ;;
    -conf)
        # Hook into -conf option
        conf=$(getoptarg "$2" "$1")
        confopts="$confopts $1 $2"
        shift 2
        ;;
    -buildconf)
        # Hook into -buildconf option
        buildconf=$(getoptarg "$2" "$1")
        confopts="$confopts $1 $2"
        shift 2
        ;;
   *)
        confopts="$confopts $1"
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

if [ -z "$target" ]
then
    target=i386-pc
fi

# Set buildconf, if need be
if [ -z "$buildconf" ]
then
    buildconf="$conf"
fi

# Export buildonly value for buildpkg
export NNBUILDONLY=$buildonly

# Set action, if need be
if [ -z "$action" ]
then
    action=all
fi

# Run the action
if [ "$action" = "configure" ]
then
    configure
elif [ "$action" = "build" ] || [ "$action" = "confbuild" ]
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
