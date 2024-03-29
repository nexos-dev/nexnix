#!/bin/sh
# cmakebuild.sh - contains CMake glue code
# Copyright 2021 - 2024 The NexNix Project
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

# Set up pkgconfig context
export PKG_CONFIG_DIR=""
export PKG_CONFIG_LIBDIR="$NNDESTDIR/usr/lib/pkgconfig:$NNDESTDIR/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=$NNDESTDIR

configure()
{
    echo "Configuring $pkg_name..."
    mkdir -p $NNOBJDIR/${pkg_name}-build && cd $NNOBJDIR/${pkg_name}-build
    if [ "$NNDEBUG" = "1" ]
    then
        buildType="Debug"
    else
        buildType="Release"
    fi
    sdkLocation=$NNDESTDIR/Programs/SDKs/NexNixSdk/0.0.1/share/NexNixSdk/cmake
    if [ "$pkg_iskernel" = "1" ]
    then
        pkg_confopts="$pkg_confopts \
-DCMAKE_USER_MAKE_RULES_OVERRIDE=$sdkLocation/SystemBuild/kernel/overrides-$NNTOOLCHAIN.cmake \
-DNEXNIX_ARCH=$NNARCH -DNEXNIX_BOARD=$NNBOARD -DNEXNIX_BASEARCH=$NNCOMMONARCH \
-DNEXNIX_TOOLCHAIN=$NNTOOLCHAIN -DNEXNIX_TARGETCONF=$NNTARGETCONF"
    fi
    if [ ! -z "$pkg_prefix" ]
    then
        pkg_confopts="$pkg_confopts -DCMAKE_INSTALL_PREFIX=$NNDESTDIR/$pkg_prefix"
    fi
    if [ "$NNUSENINJA" = "1" ]
    then
        pkg_confopts="$pkg_confopts -G Ninja"
    fi
    if [ -z "$pkg_sourcedir" ]
    then
        pkg_sourcedir="$NNSOURCEROOT/$pkg_name"
    fi
    cmake $pkg_sourcedir \
            --toolchain $sdkLocation/SystemBuild/toolchain-$NNTOOLCHAIN.cmake \
            -DCMAKE_BUILD_TYPE=$buildType -DCMAKE_SYSROOT=$NNDESTDIR \
            $pkg_confopts
    checkerr $? "unable to configure $pkg_name"
}

build()
{
    echo "Building $pkg_name..."
    if [ "$NNUSENINJA" = "1" ]
    then
        cmakegen=ninja
    else
        cmakegen=make
    fi
    cd $NNOBJDIR/${pkg_name}-build
    $cmakegen -j $NNJOBCOUNT
    checkerr $? "unable to build $pkg_name"
    $cmakegen install -j $NNJOBCOUNT
    checkerr $? "unable to install $pkg_name"
}

clean()
{
    echo "Cleaning $pkg_name..."
    rm -rf $NNOBJDIR/${pkg_name}-build
}

confhelp()
{
    ccmake $NNOBJDIR/${pkg_name}-build
}
