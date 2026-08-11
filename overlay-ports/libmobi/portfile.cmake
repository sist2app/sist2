vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bfabiszewski/libmobi
    REF "v${VERSION}"
    SHA512 dc155fddc0ef8f293fdcd5b488fe3591b9668ca04a6c11a11a218ca71288559e760233b8c54abc8d9c224b38be3877eaf72bdacfdc8044b2f6ac0a2ad60af5ab
    HEAD_REF public
)

# Upstream CMakeLists has no install rules and always builds the tools;
# replace it with a minimal static-library build
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        "-DPACKAGE_VERSION=${VERSION}"
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
