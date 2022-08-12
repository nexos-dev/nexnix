#!/bin/sh
# hostbootstrap.sh - bootstraps host tools
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

if [ "$1" != "-help" ]
then
    # Component to build
    component=$1
    if [ -z "$component" ]
    then
        panic "component not specified"
    fi
    isdash=$(echo "$1" | awk '/^-/')
    if [ ! -z "$isdash" ] && [ "$1" != "-help" ]
    then
        panic "invalid component \"$1\"" 
    fi
    shift
fi

# Argument flags
rebuild=0

while [ $# -gt 0 ]
do
    case $1 in
        -help)
            cat <<HELPEND
$(basename $0) - bootstraps host toolchain components
Usage: $0 COMPONENT [-help] [-rebuild]
  -help
                Show this help menu
  -rebuild
                Force rebuild of specified component

COMPONENT can be one of hostlibs, hosttools, toolchainlibs, 
firmware, nnpkg-host, or toolchain.
HELPEND
            exit 0
            ;;
        -rebuild)
            rebuild=1
            shift
            ;;
        *)
            panic "invalid argument $1"
            ;;
    esac
done

if [ $NNUSENINJA -eq 1 ]
then
    cmakeargs="-G Ninja"
    cmakegen=ninja
else
    cmakegen=gmake
    makeargs="--no-print-directory"
fi

if [ "$component" = "hostlibs" ]
then
    echo "Bootstraping host libraries..."
    # Download libchardet
    if [ ! -d $NNSOURCEROOT/external/tools/libraries/libchardet ] || [ "$rebuild" = "1" ]
    then
        rm -rf $NNSOURCEROOT/external/tools/libraries/libchardet
        cd $NNEXTSOURCEROOT/tools/libraries
        git clone https://github.com/nexos-dev/libchardet.git \
                  $NNSOURCEROOT/external/tools/libraries/libchardet
        checkerr $? "unable to download libchardet"
    fi
    # Build libchardet
    if ([ ! -f $NNBUILDROOT/tools/lib/libchardet.a ] && [ ! -f $NNBUILDROOT/tools/lib64/libchardet.a ]) \
       || [ "$rebuild" = "1" ]
    then
        chardetbuild="$NNBUILDROOT/build/tools/chardet-build/$cmakegen"
        mkdir -p $chardetbuild && cd $chardetbuild
        cmake $NNSOURCEROOT/external/tools/libraries/libchardet \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $cmakeargs
        checkerr $? "unable to configure libchardet"
        $cmakegen -j $NNJOBCOUNT
        checkerr $? "unable to build libchardet"
        $cmakegen install -j $NNJOBCOUNT
        checkerr $? "unable to build libchardet"
    fi
    # Build libnex
    if [ ! -f $NNBUILDROOT/tools/lib/libnex.a -a ! -f $NNBUILDROOT/tools/lib64/libnex.a ] || 
       [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            libnex_cmakeargs="$cmakeargs -DLIBNEX_ENABLE_TESTS=ON"
        else
            libnex_cmakeargs="$cmakeargs"
        fi
        libnex_builddir="$NNBUILDROOT/build/tools/libnex-build"
        mkdir -p $libnex_builddir/$cmakegen
        cd $libnex_builddir/$cmakegen
        cmake $NNSOURCEROOT/libraries/libnex \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $libnex_cmakeargs
        checkerr $? "unable to configure libnex"
        $cmakegen -j $NNJOBCOUNT $makeargs
        checkerr $? "unable to build libnex"
        $cmakegen -j $NNJOBCOUNT $makeargs install
        checkerr $? "unable to install libnex"
        ctest -V
        checkerr $? "test suite failed"
    fi
    # Build libconf
    if [ ! -f $NNBUILDROOT/tools/lib/libconf.a -a ! -f $NNBUILDROOT/tools/lib64/libconf.a ] || 
       [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            libconf_cmakeargs="$cmakeargs -DLIBCONF_ENABLE_TESTS=ON"
        else
            libconf_cmakeargs="$cmakeargs"
        fi
        libconf_builddir="$NNBUILDROOT/build/tools/libconf-build"
        mkdir -p $libconf_builddir/$cmakegen
        cd $libconf_builddir/$cmakegen
        cmake $NNSOURCEROOT/libraries/libconf \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" \
             -DLIBCONF_LINK_DEPS=ON \
             $libconf_cmakeargs
        checkerr $? "unable to configure libconf"
        $cmakegen -j $NNJOBCOUNT $makeargs
        checkerr $? "unable to build libconf"
        $cmakegen -j $NNJOBCOUNT $makeargs install
        checkerr $? "unable to install libconf"
        # Run tests
        ctest -V
        checkerr $? "test suite failed"
    fi
elif [ "$component" = "hosttools" ]
then
    echo "Bootstrapping host tools..."
    # Build host tools
    if [ ! -f "$NNBUILDROOT/tools/bin/nnimage" ] || [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            tools_cmakeargs="$cmakeargs -DTOOLS_ENABLE_TESTS=ON"
        else
            tools_cmakeargs="$cmakeargs"
        fi
        builddir=$NNBUILDROOT/build/tools/tools-build
        mkdir -p $builddir/$cmakegen
        cd $builddir/$cmakegen
        # So libuuid is found
        export PKG_CONFIG_PATH="$NNBUILDROOT/tools/lib/pkgconfig"
        cmake $NNSOURCEROOT/hosttools \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $tools_cmakeargs
        checkerr $? "unable to build host tools"
        $cmakegen -j$NNJOBCOUNT $makeargs
        checkerr $? "unable to build host tools"
        $cmakegen install -j$NNJOBCOUNT $makeargs
        checkerr $? "unable to build host tools"
        # Run tests
        ctest -V
        checkerr $? "test suite failed"
    fi
elif [ "$component" = "toolchainlibs" ]
then
    echo "Building toolchain libraries..."
    if [ "$NNTOOLCHAIN" = "gnu" ]
    then
        gmpver=6.2.1
        mpfrver=4.1.0
        mpcver=1.2.1
        islver=0.24
        cloogver=0.20.0

        # Download libgmp
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/gmp-${gmpver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/gmp/gmp-${gmpver}.tar.xz
            checkerr $? "unable to download libgmp"
            cd ../libraries
            echo "Extracting libgmp..."
            tar xf ../tarballs/gmp-${gmpver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libgmp.a ] || [ "$rebuild" = "1" ]
        then
            gmproot="$NNEXTSOURCEROOT/tools/libraries/gmp-${gmpver}"
            mkdir -p $NNBUILDROOT/build/tools/libgmp-build
            cd $NNBUILDROOT/build/tools/libgmp-build
            $gmproot/configure --disable-shared --prefix=$NNBUILDROOT/tools CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libgmp"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libgmp"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libgmp"
            make check -j$NNJOBCOUNT
            checkerr $? "libgmp test suite failed"
        fi

        # Download libmpfr
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/mpfr-${mpfrver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/mpfr/mpfr-${mpfrver}.tar.xz
            checkerr $? "unable to download libmpfr"
            cd ../libraries
            echo "Extracting libmpfr..."
            tar xf ../tarballs/mpfr-${mpfrver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libmpfr.a ] || [ "$rebuild" = "1" ]
        then
            mpfrroot="$NNEXTSOURCEROOT/tools/libraries/mpfr-${mpfrver}"
            mkdir -p $NNBUILDROOT/build/tools/libmpfr-build
            cd $NNBUILDROOT/build/tools/libmpfr-build
            $mpfrroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp=$NNBUILDROOT/tools CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libmpfr"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libmpfr"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libmpfr"
            make check -j$NNJOBCOUNT
            checkerr $? "libmpfr test suite failed"
        fi

        # Download libmpc
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/mpc-${mpcver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/mpc/mpc-${mpcver}.tar.gz
            checkerr $? "unable to download libmpc"
            cd ../libraries
            echo "Extracting libmpc..."
            tar xf ../tarballs/mpc-${mpcver}.tar.gz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libmpc.a ] || [ "$rebuild" = "1" ]
        then
            mpcroot="$NNEXTSOURCEROOT/tools/libraries/mpc-${mpcver}"
            mkdir -p $NNBUILDROOT/build/tools/libmpc-build
            cd $NNBUILDROOT/build/tools/libmpc-build
            $mpcroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp=$NNBUILDROOT/tools --with-mpfr=$NNBUILDROOT/tools\
                                CFLAGS="-O2 -DNDEBUG" CXXFLAGS="-O2 -DNDEBUG"\
            checkerr $? "unable to configure libmpc"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libmpc"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libmpc"
            make check -j$NNJOBCOUNT
            checkerr $? "libmpc test suite failed"
        fi

        # Download libisl
        if [ ! -d $NNEXTSOURCEROOT/tools/libraries/isl-${islver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://libisl.sourceforge.io/isl-0.24.tar.gz
            checkerr $? "unable to download libisl"
            cd ../libraries
            echo "Extracting libisl..."
            tar xf ../tarballs/isl-${islver}.tar.gz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libisl.a ] || [ "$rebuild" = "1" ]
        then
            islroot="$NNEXTSOURCEROOT/tools/libraries/isl-${islver}"
            mkdir -p $NNBUILDROOT/build/tools/libisl-build
            cd $NNBUILDROOT/build/tools/libisl-build
            $islroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp-prefix=$NNBUILDROOT/tools CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libisl"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libisl"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libisl"
            make check -j$NNJOBCOUNT
            checkerr $? "libisl test suite failed"
        fi

        # Download libcloog
        if [ ! -d $NNEXTSOURCEROOT/tools/libraries/cloog-${cloogver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://github.com/periscop/cloog/releases/download/cloog-${cloogver}/cloog-${cloogver}.tar.gz
            checkerr $? "unable to download libcloog"
            cd ../libraries
            echo "Extracting libcloog..."
            tar xf ../tarballs/cloog-${cloogver}.tar.gz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libcloog-isl.a ] || [ "$rebuild" = "1" ]
        then
            cloogroot="$NNEXTSOURCEROOT/tools/libraries/cloog-${cloogver}"
            mkdir -p $NNBUILDROOT/build/tools/libcloog-build
            cd $NNBUILDROOT/build/tools/libcloog-build
            $cloogroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp-prefix=$NNBUILDROOT/tools \
                                --with-isl-prefix=$NNBUILDROOT/tools \
                                --with-osl=bundled CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libcloog"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libcloog"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libcloog"
            make check -j$NNJOBCOUNT
            checkerr $? "libcloog test suite failed"
        fi
    fi
elif [ "$component" = "toolchain" ]
then
    echo "Building toolchain..."
    if [ "$NNTOOLCHAIN" = "gnu" ]
    then
        gnusys=elf
        binutilsver=2.38
        gccver=12.1.0
        # Download & build binutils
        if [ ! -d $NNEXTSOURCEROOT/tools/binutils-${binutilsver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/binutils/binutils-${binutilsver}.tar.xz
            checkerr $? "unable to download binutils"
            cd ..
            echo "Extracting binutils..."
            tar xf tarballs/binutils-${binutilsver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNTOOLCHAINPATH/$NNARCH-$gnusys-ld ] || [ "$rebuild" = "1" ]
        then
            binroot="$NNEXTSOURCEROOT/tools/binutils-${binutilsver}"
            mkdir -p $NNBUILDROOT/build/tools/binutils-build
            cd $NNBUILDROOT/build/tools/binutils-build
            $binroot/configure --prefix=$NNBUILDROOT/tools/$NNTOOLCHAIN --disable-nls \
                               --disable-shared --disable-werror --with-sysroot\
                               --enable-gold=default --target=$NNARCH-elf CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure binutils"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build binutils"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install binutils"
        fi

        # Download & build gcc
        if [ ! -d $NNEXTSOURCEROOT/tools/gcc-${gccver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/gcc/gcc-${gccver}/gcc-${gccver}.tar.xz
            checkerr $? "unable to download GCC"
            cd ..
            echo "Extracting GCC..."
            tar xf tarballs/gcc-${gccver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNTOOLCHAINPATH/$NNARCH-$gnusys-gcc ] || [ "$rebuild" = "1" ]
        then
            gccroot="$NNEXTSOURCEROOT/tools/gcc-${gccver}"
            mkdir -p $NNBUILDROOT/build/tools/gcc-build
            cd $NNBUILDROOT/build/tools/gcc-build
            $gccroot/configure --target=$NNARCH-$gnusys \
                               --disable-nls --disable-shared \
                                --without-headers --enable-languages=c,c++ \
                                --with-gmp=$NNBUILDROOT/tools --with-mpfr=$NNBUILDROOT/tools \
                                --with-mpc=$NNBUILDROOT/tools --with-isl=$NNBUILDROOT/tools \
                                --with-cloog=$NNBUILDROOT/tools \
                                CFLAGS="-O2 -DNDEBUG" CXXFLAGS="-O2 -DNDEBUG"\
                                --prefix=$NNTOOLCHAINPATH/..
            checkerr $? "unable to configure GCC"
            make all-gcc -j$NNJOBCOUNT
            checkerr $? "unable to build GCC"
            make install-gcc -j$NNJOBCOUNT
            checkerr $? "unable to install GCC"
            make all-target-libgcc -j $NNJOBCOUNT
            checkerr $? "unable to build GCC"
            make install-target-libgcc -j $NNJOBCOUNT
            checkerr $? "unable to install GCC"
        fi
    elif [ "$NNTOOLCHAIN" = "llvm" ]
    then
        llvmver=14.0.6
        if [ ! -d $NNEXTSOURCEROOT/tools/llvm-project-${llvmver}.src ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${llvmver}/llvm-project-${llvmver}.src.tar.xz
            checkerr $? "unable to download LLVM"
            cd ..
            echo "Extracting LLVM..."
            tar xf tarballs/llvm-project-${llvmver}.src.tar.xz
        fi
        if [ ! -f $NNTOOLCHAINPATH/clang ] || [ "$rebuild" = "1" ]
        then
            llvmroot="$NNEXTSOURCEROOT/tools/llvm-project-${llvmver}.src"
            mkdir -p $NNBUILDROOT/build/tools/llvm-build
            cd $NNBUILDROOT/build/tools/llvm-build
            cmake $llvmroot/llvm -DLLVM_ENABLE_PROJECTS="lld;clang" \
                  -DCMAKE_INSTALL_PREFIX=$NNTOOLCHAINPATH/.. \
                  $cmakeargs -DCMAKE_BUILD_TYPE=Release -DLLVM_PARALLEL_LINK_JOBS=1 \
                  -DLLVM_PARALLEL_COMPILE_JOBS=$NNJOBCOUNT -DLLVM_TARGETS_TO_BUILD="X86"
            checkerr $? "unable to configure LLVM"
            $cmakegen -j $NNJOBCOUNT
            checkerr $? "unable to build LLVM"
            $cmakegen install -j $NNJOBCOUNT
            checkerr $? "unable to install LLVM"
        fi

        # Build compiler-rt
        if [ ! -f $NNTOOLCHAINPATH/../lib/nexnix/libclang_rt.builtins-$NNARCH.a ] || [ "$rebuild" = "1" ]
        then
            toolsbuildroot=$NNBUILDROOT/build/tools
            buildbase=$NNEXTSOURCEROOT
            mkdir -p $toolsbuildroot/cr-build && cd $toolsbuildroot/cr-build
            # A hack to avoid errors about assert.h for now
            echo "#define assert(nothing) ((void)0)" > $NNTOOLCHAINPATH/../lib/clang/$llvmver/include/assert.h
            cmake $buildbase/tools/llvm-project-${llvmver}.src/compiler-rt \
                    -DCMAKE_CXX_FLAGS="-O3 -D__ELF__ -DNDEBUG -ffreestanding --sysroot=$NNTOOLCHAINPATH/../lib/clang/$llvmver" \
                    -DCMAKE_C_FLAGS="-O3 -D__ELF__ -DNDEBUG -ffreestanding --sysroot=$NNTOOLCHAINPATH/../lib/clang/$llvmver" \
                    -DCOMPILER_RT_USE_LIBCXX=OFF -DCOMPILER_RT_BAREMETAL_BUILD=ON \
                    -G Ninja -DCMAKE_AR=$NNTOOLCHAINPATH/llvm-ar \
                    -DCMAKE_ASM_COMPILER_TARGET=$NNARCH-elf \
                    -DCMAKE_C_COMPILER=$NNTOOLCHAINPATH/clang \
                    -DCMAKE_C_COMPILER_TARGET=$NNARCH-elf \
                    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_NM=$NNTOOLCHAINPATH/llvm-nm \
                    -DCMAKE_RANLIB=$NNTOOLCHAINPATH/llvm-ranlib \
                    -DCOMPILER_RT_BUILD_BUILTINS=ON \
                    -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_MEMPROF=OFF \
                    -DCOMPILER_RT_BUILD_PROFILE=OFF -DCOMPILER_RT_BUILD_SANITIZERS=OFF \
                    -DCOMPILER_RT_BUILD_XRAY=OFF -DLLVM_CONFIG_PATH=$NNTOOLCHAINPATH/llvm-config \
                    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
                    -DCOMPILER_RT_BUILD_ORC=ON \
                    -DCMAKE_ASM_FLAGS="-D__ELF__ -DNDEBUG -ffreestanding --sysroot=$NNTOOLCHAINPATH/../lib/clang/$llvmver" \
                    -DCMAKE_INSTALL_PREFIX=$NNTOOLCHAINPATH/.. \
                    -DCOMPILER_RT_OS_DIR=nexnix -DCOMPILER_RT_BUILTINS_ENABLE_PIC=OFF
            checkerr $? "unable to build LLVM" $0
            $cmakegen -j $NNJOBCOUNT
            checkerr $? "unable to build LLVM"
            $cmakegen -j $NNJOBCOUNT install
            checkerr $? "unable to install LLVM"
        fi
    fi
    if [ "$NNCOMMONARCH" = "x86" ]
    then
        nasmver=2.15.05
        if [ ! -d $NNEXTSOURCEROOT/tools/nasm-${nasmver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            # Download it
            wget https://www.nasm.us/pub/nasm/releasebuilds/${nasmver}/nasm-${nasmver}.tar.xz
            checkerr $? "unable to download NASM"
            cd ..
            echo "Extracting NASM..."
            tar xf tarballs/nasm-${nasmver}.tar.xz
        fi
        if [ ! -f $NNBUILDROOT/tools/bin/nasm ] || [ "$rebuild" = "1" ]
        then
            nasmroot=$NNEXTSOURCEROOT/tools/nasm-${nasmver}
            mkdir -p $NNBUILDROOT/build/tools/nasm-build
            cd $NNBUILDROOT/build/tools/nasm-build
            $nasmroot/configure --prefix=$NNBUILDROOT/tools
            checkerr $? "unable to configure NASM"
            make -j $NNJOBCOUNT
            checkerr $? "unable to build NASM"
            make install -j $NNJOBCOUNT
            checkerr $? "unable to install NASM"
        fi
        if [ "$NNFIRMWARE" = "bios" ]
        then
            dev86ver=0.16.21
            if [ ! -d $NNEXTSOURCEROOT/tools/dev86-${dev86ver} ] || [ "$rebuild" = "1" ]
            then
                mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
                wget https://github.com/lkundrak/dev86/archive/refs/tags/v$dev86ver.tar.gz \
                     -O dev86-$dev86ver.tar.gz
                checkerr $? "unable to download dev86"
                cd ..
                echo "Extracting dev86..."
                tar xf tarballs/dev86-${dev86ver}.tar.gz
            fi
            if [ ! -f $NNBUILDROOT/tools/bin/bcc ] || [ "$rebuild" = "1" ]
            then
                # Create link to dev86 source directory in build directory
                ln -sf $NNEXTSOURCEROOT/tools/dev86-${dev86ver} $NNBUILDROOT/build/tools/dev86-build
                cd $NNBUILDROOT/build/tools/dev86-build
                echo "\n" | make PREFIX=$NNBUILDROOT/tools
                checkerr $? "unable to build dev86" 
                make install
                checkerr $? "unable to install dev86"
            fi
        fi
    fi
elif [ "$component" = "firmware" ]
then
    echo "Building firmware images..."
    if [ ! -d $NNEXTSOURCEROOT/tools/edk2 ] || [ "$rebuild" = "1" ]
    then
        rm -rf $NNEXTSOURCEROOT/tools/edk2 $NNEXTSOURCEROOT/tools/edk2-platforms \
               $NNEXTSOURCEROOT/tools/edk2-non-osi
        cd $NNEXTSOURCEROOT/tools
        git clone https://github.com/tianocore/edk2.git -b edk2-stable202205
        checkerr $? "unable to download EDK2"
        cd edk2
        git submodule update --init
        checkerr $? "unable to download EDK2"
        cd ..
        git clone https://github.com/tianocore/edk2-platforms.git
        checkerr $? "unable to download EDK2"
        git clone https://github.com/tianocore/edk2-non-osi.git
        checkerr $? "unable to download EDK2"
        cd edk2-platforms
        git submodule update --init
        checkerr $? "unable to download EDK2"
    fi
    if [ ! -f $NNBUILDROOT/tools/firmware/fw${NNARCH}done ] || [ "$rebuild" = "1" ]
    then
        edk2root="$NNEXTSOURCEROOT/tools"
        export WORKSPACE=$edk2root/edk2
        export EDK_TOOLS_PATH="$WORKSPACE/BaseTools"
        export PACKAGES_PATH="$edk2root/edk2:$edk2root/edk2-platforms:$edk2root/edk2-non-osi"
        export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
        export GCC5_RISCV64_PREFIX=riscv64-linux-gnu-
        export GCC5_X64_PREFIX=x86_64-linux-gnu-
        export GCC5_IA32_PREFIX=i686-linux-gnu-
        cd $edk2root
        . edk2/edksetup.sh
        make -C edk2/BaseTools -j $NNJOBCOUNT
        # Build it
        if [ "$NNARCH" = "i386" ]
        then
            ln -sf $edk2root/edk2/Build $NNBUILDROOT/build/edk2-build
            mkdir -p $NNBUILDROOT/tools/firmware
            build -a IA32 -t GCC5 -p OvmfPkg/OvmfPkgIa32.dsc
            checkerr $? "unable to build EDK2"
            echo "Installing EDK2..."
            cp $edk2root/edk2/Build/OvmfIa32/DEBUG_GCC5/FV/OVMF_CODE.fd \
               $NNBUILDROOT/tools/firmware/OVMF_CODE_i386.fd
            cp $edk2root/edk2/Build/OvmfIa32/DEBUG_GCC5/FV/OVMF_VARS.fd \
               $NNBUILDROOT/tools/firmware/OVMF_VARS_i386.fd
            touch $NNBUILDROOT/tools/firmware/fw${NNARCH}done
        fi
    fi
elif [ "$component" = "nnpkg-host" ]
then
    echo "Bootstraping nnpkg..."
    if [ ! -d $NNEXTSOURCEROOT/tools/nnpkg ] || [ "$rebuild" = "1" ]
    then
        rm -rf $NNEXTSOURCEROOT/tools/nnpkg
        cd $NNEXTSOURCEROOT/tools
        git clone https://github.com/nexos-dev/nnpkg.git \
                  $NNSOURCEROOT/external/tools/nnpkg
        checkerr $? "unable to download nnpkg"
        cd nnpkg
        git submodule update --init
        checkerr $? "unable to download nnpkg"
    fi
    if [ ! -f $NNBUILDROOT/tools/bin/nnpkg ] || [ "$rebuild" = "1" ]
    then
        nnpkgroot="$NNEXTSOURCEROOT/tools/nnpkg"
        mkdir -p $NNBUILDROOT/build/tools/nnpkg-build
        cd $NNBUILDROOT/build/tools/nnpkg-build
        cmake $nnpkgroot -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" -DCMAKE_BUILD_TYPE=Debug \
              -DBUILD_SHARED_LIBS=OFF $cmakeargs
        checkerr $? "unable to build nnpkg"
        $cmakegen -j $NNJOBCOUNT
        checkerr $? "unable to build nnpkg"
        $cmakegen install -j $NNJOBCOUNT
        checkerr $? "unable to build nnpkg"
    fi
elif [ "$component" = "nexnix-sdk" ]
then
    echo "Bootstraping NexNix SDK..."
    nnsdk_builddir="$NNBUILDROOT/build/tools/nnsdk-build"
    mkdir -p $nnsdk_builddir/$cmakegen
    cd $nnsdk_builddir/$cmakegen
    cmake $NNSOURCEROOT/NexnixSdk \
         -DCMAKE_INSTALL_PREFIX=$NNDESTDIR/Programs/SDKs/NexNixSdk/0.0.1 \
         $cmakeargs
    checkerr $? "unable to configure NexNix SDK"
    $cmakegen -j $NNJOBCOUNT $makeargs
    checkerr $? "unable to build NexNix SDK"
    $cmakegen -j $NNJOBCOUNT $makeargs install
    checkerr $? "unable to install NexNix SDK"
    # Add to package database
    nnpkg add $NNPKGROOT/NexNixSdk/sdkPackage.conf -c $NNCONFROOT/nnpkg.conf \
        || true
elif [ "$component" = "nnpkgdb" ]
then
    echo "Initializing package database..."
    if [ ! -f $NNDESTDIR/Programs/Nnpkg/var/nnpkgdb ]
    then
        # Initialize folders
        mkdir -p $NNDESTDIR/Programs/Nnpkg/var
        mkdir -p $NNDESTDIR/Programs/Nnpkg/etc
        mkdir -p $NNDESTDIR/Programs/Index/bin
        mkdir -p $NNDESTDIR/Programs/Index/lib
        mkdir -p $NNDESTDIR/Programs/Index/etc
        mkdir -p $NNDESTDIR/Programs/Index/share
        mkdir -p $NNDESTDIR/Programs/Index/libexec
        mkdir -p $NNDESTDIR/Programs/Index/sbin
        mkdir -p $NNDESTDIR/Programs/Index/include
        mkdir -p $NNDESTDIR/Programs/Index/var
        ln -sf $NNDESTDIR/Programs/Index $NNDESTDIR/usr
        ln -sf $NNDESTDIR/Programs/Index/bin $NNDESTDIR/bin
        ln -sf $NNDESTDIR/Programs/Index/sbin $NNDESTDIR/sbin
        ln -sf $NNDESTDIR/Programs/Index/etc $NNDESTDIR/etc
        # Create a new nnpkg configuration file
        echo "settings" > $NNCONFROOT/nnpkg.conf
        echo "{" >> $NNCONFROOT/nnpkg.conf
        echo "    packageDb: \"$NNDESTDIR/Programs/Nnpkg/var/nnpkgdb\";" \
            >> $NNCONFROOT/nnpkg.conf
        echo "    strtab: \"$NNDESTDIR/Programs/Nnpkg/var/nnstrtab\";" \
            >> $NNCONFROOT/nnpkg.conf
        echo "   indexPath: '$NNDESTDIR/Programs/Index';" >> $NNCONFROOT/nnpkg.conf
        echo "}" >> $NNCONFROOT/nnpkg.conf
        nnpkg init -c $NNCONFROOT/nnpkg.conf
    fi
elif [ "$component" = "nnimage-conf" ]
then
    echo "Generating nnimage configuration..."
    # Generate list file
    echo "Programs" > $NNCONFROOT/nnimage-list.lst
    echo "System/Core" >> $NNCONFROOT/nnimage-list.lst
    echo "usr" >> $NNCONFROOT/nnimage-list.lst
    echo "bin" >> $NNCONFROOT/nnimage-list.lst
    echo "sbin" >> $NNCONFROOT/nnimage-list.lst
    echo "etc" >> $NNCONFROOT/nnimage-list.lst
    # Generate configuration file
    cd $NNCONFROOT
    echo "image nnimg" > nnimage.conf
    echo "{" >> nnimage.conf
    if [ "$NNIMGTYPE" = "mbr" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nndisk.img';" >> nnimage.conf
        echo "    sizeMul: MiB;" >> nnimage.conf
        echo "    size: 1024;" >> nnimage.conf
        echo "    format: mbr;" >> nnimage.conf
        # Ensure boot mode is BIOS
        if [ "$NNIMGBOOTMODE" != "bios" ]
        then
            panic "only BIOS boot mode is allowed on MBR disks"
        fi
        echo "    bootMode: bios;" >> nnimage.conf
        # Set path to MBR
        echo "    mbrFile: '$NNDESTDIR/System/Core/bootrec/hdmbr';" >> nnimage.conf
        echo "}" >> nnimage.conf
        # Output partitions
        echo "partition boot" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 1;" >> nnimage.conf
        echo "    size: 128;" >> nnimage.conf
        echo "    format: fat32;" >> nnimage.conf
        echo "    prefix: '/System/Core';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "    isBoot: true;" >> nnimage.conf
        echo "    vbrFile: '$NNDESTDIR/System/Core/bootrec/hdvbr';" >> nnimage.conf
        echo "}" >> nnimage.conf
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 130;" >> nnimage.conf
        echo "    size: 893;" >> nnimage.conf
        echo "    format: ext2;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "}" >> nnimage.conf
    elif [ "$NNIMGTYPE" = "gpt" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nndisk.img';" >> nnimage.conf
        echo "    sizeMul: MiB;" >> nnimage.conf
        echo "    size: 1024;" >> nnimage.conf
        echo "    format: gpt;" >> nnimage.conf
        # Check for valid boot mode
        if [ "$NNIMGBOOTMODE" = "isofloppy" ] || [ "$NNIMGBOOTMODE" = "none" ]
        then
            panic "invalid boot mode specified"
        fi
        echo "    bootMode: $NNIMGBOOTMODE;" >> nnimage.conf
        # Set path to MBR if needed
        if [ "$NNIMGBOOTMODE" != "efi" ]
        then
            echo "    mbrFile: '$NNDESTDIR/System/Core/bootrec/gptmbr';" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
        # Output partitions
        echo "partition boot" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 1;" >> nnimage.conf
        echo "    size: 128;" >> nnimage.conf
        echo "    format: fat32;" >> nnimage.conf
        echo "    prefix: '/System/Core';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "    isBoot: true;" >> nnimage.conf
        if [ "$NNIMGBOOTMODE" != "efi" ]
        then
            echo "    vbrFile: '$NNDESTDIR/System/Core/bootrec/hdvbr';" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 130;" >> nnimage.conf
        echo "    size: 893;" >> nnimage.conf
        echo "    format: ext2;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "}" >> nnimage.conf
    elif [ "$NNIMGTYPE" = "iso9660" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nncdrom.iso';" >> nnimage.conf
        echo "    sizeMul: KiB;" >> nnimage.conf
        echo "    format: iso9660;" >> nnimage.conf
        if [ "$NNIMGBOOTMODE" = "isofloppy" ]
        then
            echo "    bootMode: noboot;" >> nnimage.conf
        else
            echo "    bootMode: $NNIMGBOOTMODE;" >> nnimage.conf
            if [ "$NNIMGBOOTMODE" != "efi" ]
            then
                echo "    bootEmu: $NNIMGBOOTEMU;" >> nnimage.conf
            fi
            if [ "$NNIMGUNIVERSAL" = "0" ]
            then
                echo "    isUniversal: false;" >> nnimage.conf
            else
                echo "    isUniversal: true;" >> nnimage.conf
            fi
        fi
        if [ "$NNIMGBOOTMODE" = "isofloppy" ]
        then
            # Write out boot floppy
            echo "}" >> nnimage.conf
            echo "image nnboot" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    defaultFile: '$NNCONFROOT/nndisk.flp';" >> nnimage.conf
            echo "    sizeMul: KiB;" >> nnimage.conf
            echo "    size: 1440;" >> nnimage.conf
            echo "    format: floppy;" >> nnimage.conf
            echo "    mbrFile: '$NNDESTDIR/System/Core/bootrec/flpmbr';" >> nnimage.conf
            echo "partition bootpart" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    size: 1440;" >> nnimage.conf
            echo "    prefix: '/System/Core';" >> nnimage.conf
            echo "    format: fat12;" >> nnimage.conf
            echo "    isBoot: true;" >> nnimage.conf
            echo "    image: nnboot;" >> nnimage.conf
            echo "}" >> nnimage.conf
        elif [ "$NNIMGBOOTEMU" = "noemu" ]
        then
            echo "    mbrFile: '$NNDESTDIR/System/Core/bootrec/isombr';" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
        # Create partitions
        if [ "$NNIMGBOOTMODE" = "bios" ] || [ "$NNIMGBOOTMODE" = "hybrid" ]
        then
            if [ "$NNIMGBOOTEMU" = "hdd" ]
            then
                echo "partition boot" >> nnimage.conf
                echo "{" >> nnimage.conf
                echo "    size: 131072;" >> nnimage.conf
                echo "    format: fat32;" >> nnimage.conf
                echo "    isBoot: true;"  >> nnimage.conf
                echo "    prefix: '/System/Core';" >> nnimage.conf
                echo "    image: nnimg"; >> nnimage.conf
                echo "}" >> nnimage.conf
            elif [ "$NNIMGBOOTEMU" = "fdd" ]
            then
                echo "partition boot" >> nnimage.conf
                echo "{" >> nnimage.conf
                echo "    size: 1440;" >> nnimage.conf
                echo "    format: fat12;" >> nnimage.conf
                echo "    isBoot: true;"  >> nnimage.conf
                echo "    prefix: '/System/Core';" >> nnimage.conf
                echo "    image: nnimg"; >> nnimage.conf
                echo "}" >> nnimage.conf
            fi
        fi
        if [ "$NNIMGBOOTMODE" = "efi" ]
        then
            echo "partition boot" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    format: fat32;" >> nnimage.conf
            echo "    size: 131072;" >> nnimage.conf
            echo "    isBoot: true;" >> nnimage.conf
            echo "    prefix: '/System/Core';" >> nnimage.conf
            echo "    image: nnimg;" >> nnimage.conf
            echo "}" >> nnimage.conf
        fi
        if [ "$NNIMGBOOTMODE" = "hybrid" ]
        then
            echo "partition efiboot" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    format: fat32;" >> nnimage.conf
            echo "    size: 131072;" >> nnimage.conf
            echo "    isAltBoot: true;" >> nnimage.conf
            echo "    prefix: '/System/Core';" >> nnimage.conf
            echo "    image: nnimg;" >> nnimage.conf
            echo "}" >> nnimage.conf
        fi
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    format: iso9660;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        if [ "$NNIMGBOOTEMU" = "noemu" ]
        then
            echo "    isBoot: true;" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
    fi
else
    panic "invalid component $component specified"
fi
