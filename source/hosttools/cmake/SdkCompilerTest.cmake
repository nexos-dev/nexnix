#[[
    SdkCompilerTest.cmake - contains standard compiler tests
    Copyright 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    There should be a copy of the License distributed in a file named
    LICENSE, if not, you may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License
]]

include(CheckCSourceCompiles)

# Checks for one of __attribute__((visibility)) or __declspec(dll*)
# Sets var1 and var2
function(check_library_visibility var1 var2)
    # Check for __declspec(dllexport)
    check_c_source_compiles([=[
        __declspec(dllexport) int f() {}
        int main() {}
        ]=] ${var1})
    # Check for __attribute__((visibility))
    check_c_source_compiles([=[
        __attribute__((visibility("default"))) int f() {}
        int main(){}
    ]=] ${var2})
endfunction()
