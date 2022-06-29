#!/bin/sh
# buildtoolchain.sh - build host toolchain
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

# Check if the user wants to rebuild the toolchain
rebuild=0
if [ "$1" = "rebuild" ] || [ "$NNTOOLCHAIN_REBUILD" = "1" ] || [ "$1" = "rebuild_noclean" ]
then
    rebuild=1
    # Remove old stuff
    if [ "$NNTOOLCHAIN_NOCLEAN" != "1" ] && [ "$1" != "rebuild_noclean" ]
    then
        rm -rf $NNBUILDROOT/tools $NNBUILDROOT/build/tools/*
    fi
fi

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

# Determine cmake generator
if [ $NNUSENINJA -eq 1 ]
then
    cmakeargs="-G Ninja"
    cmakegen=ninja
else
    cmakegen=gmake
    makeargs="--no-print-directory -Otarget"
fi

# Build the host cross compiler
if [ "$NNTOOLCHAIN" = "gnu" ]
then
    # Dependency versions
    gccver=11.2.0
    binutilsver=2.37
    gmpver=6.2.1
    mpcver=1.2.1
    mpfrver=4.1.0
    islver=0.24
    cloogver=0.20.0
    gnusys=elf
    # Build everything
    if [ ! -f $NNTOOLCHAINPATH/$NNARCH-$gnusys-gcc ] || [ "$rebuild" = "1" ]
    then
        echo "Building host toolchain GNU..."
        cd $NNEXTSOURCEROOT
        toolprefix=$NNBUILDROOT/tools
        toolsbuildroot=$NNBUILDROOT/build/tools
        buildbase=$NNEXTSOURCEROOT

        # Build GMP
        mkdir -p $toolsbuildroot/gmp-build && cd $toolsbuildroot/gmp-build
        # Configure it
        $buildbase/gmp-${gmpver}/configure --prefix=$toolprefix --disable-shared CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure GMP"
        # Build it
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build GMP"
        gmake check -j$NNJOBCOUNT
        checkerr $? "GMP isn't clean"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install GMP"

        # Build MPFR
        mkdir -p $toolsbuildroot/mpfr-build && cd $toolsbuildroot/mpfr-build
        # Configure it
        $buildbase/mpfr-${mpfrver}/configure --prefix=$toolprefix --with-gmp=$toolprefix --disable-shared \
                                             CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure MPFR"
        # Build it
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build MPFR"
        gmake check -j$NNJOBCOUNT
        checkerr $? "MPFR isn't clean"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install MPFR"

        # Build MPC
        mkdir -p $toolsbuildroot/mpc-build && cd $toolsbuildroot/mpc-build
        # Configure it
        $buildbase/mpc-${mpcver}/configure --prefix=$toolprefix --with-gmp=$toolprefix --with-mpfr=$toolprefix \
                                           --disable-shared CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure MPC"
        # Build it
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build MPC"
        gmake check -j$NNJOBCOUNT
        checkerr $? "MPC isn't clean"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install MPC"

        # Build ISL
        mkdir -p $toolsbuildroot/isl-build && cd $toolsbuildroot/isl-build
        # Configure it
        $buildbase/isl-${islver}/configure --prefix=$toolprefix --with-gmp-prefix=$toolprefix --disable-shared \
                                           CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure ISL"
        # Build it
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build ISL"
        gmake check -j$NNJOBCOUNT
        checkerr $? "ISL isn't clean"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install ISL"

        # Build CLooG
        mkdir -p $toolsbuildroot/cloog-build && cd $toolsbuildroot/cloog-build
        # Configure it
        $buildbase/cloog-${cloogver}/configure --prefix=$toolprefix --with-gmp-prefix=$toolprefix \
                                               --with-isl-prefix=$toolprefix --with-osl=bundled \
                                               --disable-shared CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure CLooG"
        # Build it
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build CLooG"
        gmake check -j$NNJOBCOUNT
        checkerr $? "CLooG isn't clean"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install CLooG"

        # Build binutils
        mkdir -p $toolsbuildroot/binutils-build && cd $toolsbuildroot/binutils-build
        # Configure it
        $buildbase/binutils-${binutilsver}/configure --target=$NNARCH-$gnusys --disable-nls --disable-shared \
                                                     --with-sysroot --enable-gold=yes --disable-werror \
                                                     --prefix=$NNTOOLCHAINPATH/.. CFLAGS="-O2 -DNDEBUG"
        checkerr $? "unable to configure binutils"
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build binutils"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install binutils"

        # Build GCC
        mkdir -p $toolsbuildroot/gcc-build && cd $toolsbuildroot/gcc-build
        # Configure it
        $buildbase/gcc-${gccver}/configure --target=$NNARCH-$gnusys --disable-nls --disable-shared \
                                                     --without-headers --enable-languages=c,c++ \
                                                     --with-gmp=$toolprefix --with-mpfr=$toolprefix \
                                                     --with-mpc=$toolprefix --with-isl=$toolprefix \
                                                     --with-cloog=$toolprefix CFLAGS="-O2 -DNDEBUG" \
                                                     CXXFLAGS="-O2 -DNDEBUG" --prefix=$NNTOOLCHAINPATH/..
        checkerr $? "unable to configure GCC"
        gmake all-gcc -j$NNJOBCOUNT
        checkerr $? "unable to build GCC"
        gmake all-target-libgcc -j$NNJOBCOUNT
        checkerr $? "unable to build GCC"
        gmake install-gcc -j$NNJOBCOUNT
        checkerr $? "unable to install GCC"
        gmake install-target-libgcc -j$NNJOBCOUNT
        checkerr $? "unable to install GCC"
    fi
elif [ "$NNTOOLCHAIN" = "llvm" ]
then
    llvmver=13.0.1
    llvmtargets="X86"
    # Build it
    if [ ! -f $NNTOOLCHAINPATH/clang ] || [ "$rebuild" = "1" ]
    then
        toolsbuildroot=$NNBUILDROOT/build/tools
        ln -sf $NNEXTSOURCEROOT $NNBUILDROOT/external
        buildbase=$NNEXTSOURCEROOT
        mkdir -p $toolsbuildroot/llvm-build && cd $toolsbuildroot/llvm-build
        # Configure it
        cmake $buildbase/llvm-project-${llvmver}.src/llvm -DLLVM_ENABLE_PROJECTS="lld;clang" \
                  -DCMAKE_INSTALL_PREFIX=$NNTOOLCHAINPATH/.. \
                  $cmakeargs -DCMAKE_BUILD_TYPE=Release -DLLVM_PARALLEL_LINK_JOBS=1 \
                  -DLLVM_PARALLEL_COMPILE_JOBS=$NNJOBCOUNT -DLLVM_TARGETS_TO_BUILD=$llvmtargets
        checkerr $? "unable to build LLVM"
        # Build it
        $cmakegen -j $NNJOBCOUNT
        checkerr $? "unable to build LLVM"
        $cmakegen -j $NNJOBCOUNT install
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
        cmake $buildbase/llvm-project-${llvmver}.src/compiler-rt \
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
else
    panic "invalid toolchain $NNTOOLCHAIN"
fi

# Build NASM
if [ "$NNCOMMONARCH" = "x86" ]
then
    nasmver=2.15.05
    cd $NNEXTSOURCEROOT
    # Build it if that is needed
    if [ ! -f $NNTOOLCHAINPATH/../../bin/nasm ]
    then
        echo "Building NASM..."
        nasmbuildir=$NNBUILDROOT/build/tools/nasm-build
        buildbase=$NNEXTSOURCEROOT
        mkdir -p  $nasmbuilddir && cd $nasmbuildir
        $buildbase/configure --prefix=$NNTOOLCHAINPATH/../..
        checkerr $? "unable to configure NASM"
        gmake -j$NNJOBCOUNT
        checkerr $? "unable to build NASM"
        gmake install -j$NNJOBCOUNT
        checkerr $? "unable to install NASM"
    fi
fi
