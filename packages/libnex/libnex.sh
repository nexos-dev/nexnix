# libnex.sh - contains libnex buildpkg configuration
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

pkg_name=libnex
pkg_iskernel=1
pkg_buildsys=cmake
pkg_prefix="/Programs/libc"
pkg_confopts="-DLIBNEX_BAREMETAL=ON -DLIBNEX_ENABLE_NLS=OFF"
pkg_sourcedir=$NNSOURCEROOT/libraries/libnex
