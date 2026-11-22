# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(pop-pad CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(POPPAD_WERROR "Treat compiler warnings as errors (CI turns this on)" OFF)

add_library(poppad SHARED
    src/main.cpp
    src/log.cpp
    src/iat.cpp
    src/di_hook.cpp
    src/xi_hook.cpp
    src/d3d_hook.cpp
    src/cmd.cpp
    src/profile_fix.cpp
    src/fs_hook.cpp
    src/font_file.cpp
    src/font_patch.cpp
    src/glyphs.cpp
    src/hid_scan.cpp
    src/icons.cpp
    src/pad_style.cpp
    src/brackets.cpp
    src/dinput8.def
)

# The game loads this as dinput8.dll from its own folder, ahead of the system copy.
set_target_properties(poppad PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")
# GetProcAddress hands back a generic pointer, so casting it to the real signature is unavoidable.
target_compile_options(poppad PRIVATE -O2 -Wall -Wextra -Wno-cast-function-type -fno-exceptions -fno-rtti)
if(POPPAD_WERROR)
    target_compile_options(poppad PRIVATE -Werror)
endif()
target_link_options(poppad PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup)
target_link_libraries(poppad PRIVATE user32 kernel32 advapi32 setupapi hid)
