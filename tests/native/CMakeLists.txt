# SPDX-License-Identifier: GPL-3.0-or-later
# Unit tests for the parts of pop-pad that do not need Windows. Built with the host compiler:
#   cmake -S tests/native -B build/native && cmake --build build/native && ctest --test-dir build/native
cmake_minimum_required(VERSION 3.20)
project(pop-pad-native-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(POPPAD_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
option(POPPAD_WERROR "Treat compiler warnings as errors" OFF)

# test_main.cpp includes font_patch.cpp itself, to reach the functions that file keeps private.
add_executable(native_tests test_main.cpp ../../src/font_file.cpp ../../src/icons.cpp ../../src/pad_style.cpp)
target_include_directories(native_tests BEFORE PRIVATE shim ../../src)
target_compile_options(native_tests PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti -g)
# The paths are built with bounded snprintf calls; only the host compiler worries they could be cut.
target_compile_options(native_tests PRIVATE -Wno-format-truncation)

if(POPPAD_WERROR)
  target_compile_options(native_tests PRIVATE -Werror)
endif()
if(POPPAD_SANITIZE)
  # Reads past the end of a font or archive do not change a result, so only a sanitizer sees them.
  target_compile_options(native_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(native_tests PRIVATE -fsanitize=address,undefined)
endif()

enable_testing()
add_test(NAME native COMMAND native_tests)
