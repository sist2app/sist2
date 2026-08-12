vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO shyyio/libantiword2
    REF e7f717d90dc66031f0f70612e692edd0f2f178d0
    SHA512 9ce04a0ad223e88503f383cd4877564170fcf7a4751213739d6b07f64e63292be9f02d2b18d4da27b6be30bc459fa741c9b1f34d8b4e0a8ef91885c2aec6edcc
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DAW2_BUILD_TESTS=OFF
        -DAW2_BUILD_TOOLS=OFF
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
