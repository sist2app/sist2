vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bfabiszewski/libmobi
    REF "v${VERSION}"
    SHA512 414f1afbdf65158df12476955b48df605015af94a7a9c5e40d286ede5266329c9073bc107c0ae62d3e5399252cd574ae63ece6a5550ea4b2326b64ed808026aa
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
