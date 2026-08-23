vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO shyyio/libenvelope
    REF 608536802a26bc15d003520073263033cde6da23
    SHA512 4d2ed40f527b8fe1315a2147e34515504d05a32f5cd98d39c2659397da044f94b6501634eaf21abe860d57a8067dd87ad8610387ffbbb4bdef268f1cad00cfde
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DEV_BUILD_TESTS=OFF
        -DEV_BUILD_TOOLS=OFF
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
