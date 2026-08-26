vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

set(LIB_VERSION 20231205)

# The release distribution ships configured sources; the repository does not
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/libyal/libpff/releases/download/${LIB_VERSION}/libpff-alpha-${LIB_VERSION}.tar.gz"
    FILENAME "libpff-alpha-${LIB_VERSION}.tar.gz"
    SHA512 21484bd955a21147619e6d999f3e64d12c8b63e5c44a9c986446ebf1ff44798aa593c7c685e65c559e73a7661efe13d25d43cafc544f1955f3c71d9f16b87718
)

# The patch is git format-patch output against the release, meant to be sent upstream; drop it
# from the port once it lands
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    SOURCE_BASE "${LIB_VERSION}"
    PATCHES
        0001-Free-the-descriptor-index-value-in-libpff_folder_det.patch
)

# Upstream's own build. The vcpkg port builds the sources with a CMakeLists of its own instead,
# which skips the generated config.h and only holds together under MSVC.
#
# --without-zlib: the deflate used by a handful of PST data blocks is implemented in libpff itself
# as well, and the configure check for the library looks for -lz, which is not what the zlib port
# is called on mingw.
# COPY_SOURCE: libyal's Makefiles include headers of one bundled library from another through the
# build tree only, so a VPATH build cannot find them
vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    COPY_SOURCE
    OPTIONS
        --disable-nls
        --without-zlib
        --disable-python
        --disable-java
        --enable-multi-threading-support
        --enable-wide-character-type
)

vcpkg_install_make()

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/bin"
    "${CURRENT_PACKAGES_DIR}/debug/bin"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/man"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
