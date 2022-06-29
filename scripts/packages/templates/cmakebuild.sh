#!/bin/sh
# cmakebuild.sh - contains CMake build template
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

# Standard stuff
. $NNPROJECTROOT/scripts/packages/templates/basetemplate.sh

# Configure function
configure()
{
    echo "Configuring $pkg_name..."
    # Prepare object directory
    mkdir -p $NNOBJDIR/${pkg_name}-build && cd $NNOBJDIR/${pkg_name}-build
    # Set build type to debug or release
    if [ "$NNDEBUG" = "1" ]
    then
        buildType="Debug"
    else
        buildType="Release"
    fi

    sdkLocation=$NNDESTDIR/Programs/NexNixSdk
    # Set kernel flags
    if [ "$pkg_iskernel" = "1"  ]
    then
        pkg_confopts="$pkg_confopts 
                -DCMAKE_USER_MAKE_RULES_OVERRIDE=$sdkLocation/systemBuild/kernel/overrides-${NNTOOLCHAIN}.cmake \
                -DNEXNIX_ARCH=$NNARCH -DNEXNIX_BOARD=$NNBOARD -DNEXNIX_COMMONARCH=$NNCOMMONARCH \
                -DNEXNIX_TOOLCHAIN=$NNTOOLCHAIN"
    fi
    if [ ! -z "$pkg_prefix" ]
    then
        pkg_confopts="$pkg_confopts -DCMAKE_INSTALL_PREFIX=$pkg_prefix"
    fi
    cmake $pkg_source -G "Ninja" \
            -DCMAKE_TOOLCHAIN_FILE=$NNDESTDIR/Programs/SDKs/NexNix/SystemBuild/toolchain-${NNTOOLCHAIN}.cmake \
            -DCMAKE_BUILD_TYPE=$buildType -DCMAKE_SYSROOT=$NNDESTDIR \
            $pkg_confopts \
            $(eval echo \$$(echo "PKG_${pkg_name}_CONFOPTS"))
    checkerr $? "unable to configure $pkg_name"
}

# Build function
build()
{
    echo "Building $pkg_name..."
    ninja -C $NNOBJDIR/${pkg_name}-build -j$NNJOBCOUNT
    checkerr $? "unable to build $pkg_name"
}

# Install function
install()
{
    echo "Installing $pkg_name..."
    ninja -C $NNOBJDIR/${pkg_name}-build -j$NNJOBCOUNT install
    checkerr $? "unable to install $pkg_name"
}

# Clean function
clean()
{
    echo "Cleaning $pkg_name..."
    ninja -C $NNOBJDIR/${pkg_name}-build -j$NNJOBCOUNT clean
    checkerr $? "unable to clean $pkg_name"
}

# For configuration help
confhelp()
{
    ccmake $NNOBJDIR/${pkg_name}-build
    checkerr $? "unable to edit configuration of $pkg_name"
}
