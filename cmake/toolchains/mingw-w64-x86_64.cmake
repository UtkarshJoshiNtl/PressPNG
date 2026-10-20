# Cross-compile toolchain: Linux host -> Windows x86-64 with MinGW-w64.
# Usage:
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
#
# If your MinGW is installed outside /usr, set MINGW_PREFIX (the directory
# that contains bin/x86_64-w64-mingw32-g++-posix), e.g.
#   -DMINGW_PREFIX=$HOME/mingw/usr

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED MINGW_PREFIX)
    set(MINGW_PREFIX "/usr" CACHE PATH "MinGW installation prefix")
endif()

set(CMAKE_C_COMPILER   "${MINGW_PREFIX}/bin/x86_64-w64-mingw32-gcc-posix")
set(CMAKE_CXX_COMPILER "${MINGW_PREFIX}/bin/x86_64-w64-mingw32-g++-posix")
set(CMAKE_RC_COMPILER  "${MINGW_PREFIX}/bin/x86_64-w64-mingw32-windres")

# Debian/Ubuntu split the target sysroot into its own package directory.
if(EXISTS "${MINGW_PREFIX}/x86_64-w64-mingw32")
    set(_mingw_sysroot "${MINGW_PREFIX}/x86_64-w64-mingw32")
    string(APPEND CMAKE_CXX_FLAGS_INIT " --sysroot=${_mingw_sysroot}")
    string(APPEND CMAKE_C_FLAGS_INIT " --sysroot=${_mingw_sysroot}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " --sysroot=${_mingw_sysroot}")
endif()

set(CMAKE_FIND_ROOT_PATH "${MINGW_PREFIX}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
