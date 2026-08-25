# Cross-compilation toolchain for the Windows binary, chainloaded by vcpkg
# (-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE). The -posix compiler variants are required: the win32 thread
# model ships no winpthreads, and both sist2 and tesseract use pthreads.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

# The root path stays restrictive so that find_library() cannot reach the host's Linux libraries.
# vcpkg's own prefix has to be added to it explicitly for the same reason it works elsewhere.
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

foreach (vcpkg_root IN ITEMS "${VCPKG_INSTALLED_DIR}" "${_VCPKG_INSTALLED_DIR}")
    if (vcpkg_root AND VCPKG_TARGET_TRIPLET)
        list(APPEND CMAKE_FIND_ROOT_PATH "${vcpkg_root}/${VCPKG_TARGET_TRIPLET}")
    endif ()
endforeach ()
list(REMOVE_DUPLICATES CMAKE_FIND_ROOT_PATH)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
