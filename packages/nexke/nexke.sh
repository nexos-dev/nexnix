# nexke.sh - contains nexke buildpkg configuration
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

if [ "$NNTARGETISMP" = "1" ]
then
    uparg="-DNEXKE_UP=0"
else
    uparg="-DNEXKE_UP=1"
fi

pkg_name=nexke
pkg_iskernel=1
pkg_buildsys=cmake
pkg_prefix="/System/Core/Boot"
pkg_confopts="-DNEXNIX_LOGLEVEL=$NNLOGLEVEL -DNEXNIX_GRAPHICS_MODE=$NNGRAPHICSMODE $uparg"
if [ "$NNARCH" = "i386" ]
then
    if [ "$NNISPAE" = "1" ]
    then
        pkg_confopts="$pkg_confopts -DNEXNIX_I386_PAE=ON"
    else
        pkg_confopts="$pkg_confopts -DNEXNIX_I386_PAE=OFF"
    fi
elif [ "$NNARCH" = "x86_64" ]
then
    if [ "$NNISLA57" = "1" ]
    then
        pkg_confopts="$pkg_confopts -DNEXNIX_X86_64_LA57=ON"
    else
        pkg_confopts="$pkg_confopts -DNEXNIX_X86_64_LA57=OFF"
    fi
fi
