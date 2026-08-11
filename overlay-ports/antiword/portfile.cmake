vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sist2app/antiword
    REF ddb042143e72a8b789e06f09dbc897dfa9f15b82
    SHA512 f849b832e571f216d9129b9ace525bdd5e86a6bd74f40bc8012cf6da41b83ad9fa215e13aeebd302cc35b82d6a185364aa0d17f465a91f0163e4818f5bd0d0ea
    HEAD_REF master
)

# The fork's CMakeLists has no install rules; replace it with one that installs
# the static library and the public headers
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
