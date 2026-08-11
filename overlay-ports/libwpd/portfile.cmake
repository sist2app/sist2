vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_download_distfile(ARCHIVE
    URLS "https://downloads.sourceforge.net/project/libwpd/libwpd/libwpd-${VERSION}/libwpd-${VERSION}.tar.gz"
    FILENAME "libwpd-${VERSION}.tar.gz"
    SHA512 ea17c26d3e888e5573649da0e3c5cfa6ab1af27908dffb064a8a7b26368c1b54b451f967e17ec52ab61bcd771ffe645213a9b8e0917b281a7f067f4750f076c0
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# Upstream builds with autotools; replace it with a minimal static-library build
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.LGPL" "${SOURCE_PATH}/COPYING.MPL")
