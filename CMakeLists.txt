cmake_minimum_required(VERSION 3.15)

# Allow configuring bundled third-party projects that declare a pre-3.5 minimum
# (e.g. third-party/argparse) with CMake 4.x
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(sist2)
set(CMAKE_C_STANDARD 11)

option(SIST_DEBUG "Build a debug executable" on)
option(SIST_FAST "Enable more optimisation flags" off)
option(SIST_DEBUG_INFO "Turn on debug information in web interface" on)
option(SIST_STATIC "Produce a fully static executable (musl only)" off)
option(BUILD_TESTS "Build libscan tests" on)

add_compile_definitions(
        "SIST_PLATFORM=${SIST_PLATFORM}"
)

if (SIST_DEBUG)
    add_compile_definitions(
            "SIST_DEBUG=${SIST_DEBUG}"
    )
    set(VCPKG_BUILD_TYPE debug)
else ()
    set(VCPKG_BUILD_TYPE release)
endif ()

if (SIST_DEBUG_INFO)
    add_compile_definitions(
            "SIST_DEBUG_INFO=${SIST_DEBUG_INFO}"
    )
endif ()

set(ARGPARSE_SHARED off)
add_subdirectory(third-party/argparse)

add_subdirectory(third-party/antiword)

set(USE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(USE_XMLWRITER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/libmobi)

#
# libscan — file parsing library (text/metadata/thumbnail extraction)
#
add_library(
        scan
        libscan/util.c libscan/util.h
        libscan/scan.h
        libscan/macros.h

        libscan/text/text.c libscan/text/text.h
        libscan/arc/arc.c libscan/arc/arc.h
        libscan/ebook/ebook.c libscan/ebook/ebook.h
        libscan/comic/comic.c libscan/comic/comic.h
        libscan/ooxml/ooxml.c libscan/ooxml/ooxml.h
        libscan/media/media.c libscan/media/media.h
        libscan/font/font.c libscan/font/font.h
        libscan/msdoc/msdoc.c libscan/msdoc/msdoc.h
        libscan/json/json.c libscan/json/json.h
        libscan/wpd/wpd.c libscan/wpd/wpd.h libscan/wpd/libwpd_c_api.h libscan/wpd/libwpd_c_api.cpp
        libscan/mobi/scan_mobi.c libscan/mobi/scan_mobi.h
        libscan/raw/raw.c libscan/raw/raw.h

        third-party/utf8.h/utf8.h
)
set_target_properties(scan PROPERTIES LINKER_LANGUAGE C)

add_executable(
        sist2
        # argparse
        third-party/argparse/argparse.h third-party/argparse/argparse.c

        src/main.c
        src/sist.h
        src/io/walk.h src/io/walk.c
        src/tpool.h src/tpool.c
        src/parsing/parse.h src/parsing/parse.c
        src/parsing/magic_util.c src/parsing/magic_util.h
        src/io/serialize.h src/io/serialize.c
        src/parsing/mime.h src/parsing/mime.c src/parsing/mime_generated.c
        src/index/web.c src/index/web.h
        src/web/serve.c src/web/serve.h
        src/web/web_util.c src/web/web_util.h
        src/index/elastic.c src/index/elastic.h
        src/util.c src/util.h
        src/ctx.c src/ctx.h
        src/types.h
        src/log.c src/log.h
        src/cli.c src/cli.h
        src/database/database.c src/database/database.h
        src/parsing/fs_util.h

        src/auth0/auth0_c_api.h src/auth0/auth0_c_api.cpp

        src/database/database_stats.c
        src/database/database_schema.c
        src/database/database_fts.c
        src/web/web_fts.c
        src/database/database_embeddings.c
        src/ignorelist.c
        src/ignorelist.h)
set_target_properties(sist2 PROPERTIES LINKER_LANGUAGE C)

target_link_directories(sist2 PRIVATE BEFORE ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/)

find_package(PkgConfig REQUIRED)

find_package(cJSON CONFIG REQUIRED)
find_package(unofficial-mongoose CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_library(MAGIC_LIB NAMES libmagic.a REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenBLAS CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)

find_package(LibArchive REQUIRED)
find_package(BZip2 REQUIRED)
find_package(lz4 REQUIRED)

find_package(Threads REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(unofficial-pcre CONFIG REQUIRED)

find_library(JBIG2DEC_LIB NAMES jbig2decd jbig2dec)
find_library(HARFBUZZ_LIB NAMES harfbuzz harfbuzzd)
find_library(LZO2_LIB NAMES lzo2)

find_library(MUPDF_LIB NAMES liblibmupdf.a)
find_library(CMS_LIB NAMES lcms2)
find_library(JAS_LIB NAMES jasper)
find_library(GUMBO_LIB NAMES gumbo)
find_library(GOMP_LIB NAMES libgomp.a gomp
        PATHS
        /usr/lib/gcc/x86_64-linux-gnu/5/
        /usr/lib/gcc/x86_64-linux-gnu/6/
        /usr/lib/gcc/x86_64-linux-gnu/7/
        /usr/lib/gcc/x86_64-linux-gnu/8/
        /usr/lib/gcc/x86_64-linux-gnu/9/
        /usr/lib/gcc/x86_64-linux-gnu/10/
        /usr/lib/gcc/x86_64-linux-gnu/11/
        /usr/lib/gcc/x86_64-linux-gnu/12/

        /usr/lib/gcc/aarch64-linux-gnu/5/
        /usr/lib/gcc/aarch64-linux-gnu/6/
        /usr/lib/gcc/aarch64-linux-gnu/7/
        /usr/lib/gcc/aarch64-linux-gnu/8/
        /usr/lib/gcc/aarch64-linux-gnu/9/
        /usr/lib/gcc/aarch64-linux-gnu/10/
        /usr/lib/gcc/aarch64-linux-gnu/11/
        /usr/lib/gcc/aarch64-linux-gnu/12/
)
find_package(Leptonica CONFIG REQUIRED)
find_package(libraw CONFIG REQUIRED)
find_package(Freetype REQUIRED)

find_package(FFMPEG REQUIRED)
list(REMOVE_ITEM FFMPEG_LIBRARIES /usr/lib/x86_64-linux-gnu/libm.a)
list(REMOVE_ITEM FFMPEG_LIBRARIES /usr/lib/aarch64-linux-gnu/libm.a)

#
# Per-configuration compile flags (shared by scan, sist2 and antiword)
#
if (SIST_DEBUG)
    # antiword.h requires exactly one of DEBUG/NDEBUG
    target_compile_definitions(scan PRIVATE DEBUG)
    set(SIST_SANITIZE_OPTIONS
            -g
            -fstack-protector
            -fno-omit-frame-pointer
            -fsanitize=address
            -fno-inline
    )
    target_compile_options(antiword PRIVATE ${SIST_SANITIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_SANITIZE_OPTIONS})
    target_link_options(
            sist2
            PRIVATE
            -fsanitize=address
            -static-libasan
    )
    set_target_properties(
            sist2
            PROPERTIES
            OUTPUT_NAME sist2_debug
    )
elseif (SIST_FAST)
    target_compile_definitions(scan PRIVATE NDEBUG)
    set(SIST_OPTIMIZE_OPTIONS
            -Ofast
            -march=native
            -fno-stack-protector
            -fomit-frame-pointer
            -freciprocal-math
    )
    target_compile_options(scan PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_OPTIMIZE_OPTIONS})
else ()
    target_compile_definitions(scan PRIVATE NDEBUG)
    set(SIST_OPTIMIZE_OPTIONS
            -Ofast
            -fno-stack-protector
            -fomit-frame-pointer
            -w
    )
    target_compile_options(scan PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_OPTIMIZE_OPTIONS})
endif ()

target_compile_options(
        scan
        PRIVATE
        -g
)

include(ExternalProject)
ExternalProject_Add(
        libwpd
        URL http://prdownloads.sourceforge.net/libwpd/libwpd-0.9.9.tar.gz

        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        TEST_COMMAND ""
        CONFIGURE_COMMAND ./configure --without-docs --enable-static --disable-shared
        INSTALL_COMMAND ""

        PREFIX "third-party/ext_libwpd"
        SOURCE_DIR "third-party/ext_libwpd/src/libwpd"
        BINARY_DIR "third-party/ext_libwpd/src/libwpd"

        BUILD_COMMAND make -j33
)
SET(WPD_LIB_DIR ${CMAKE_BINARY_DIR}/third-party/ext_libwpd/src/libwpd/src/lib/.libs/)
SET(WPD_INCLUDE_DIR ${CMAKE_BINARY_DIR}/third-party/ext_libwpd/src/libwpd/inc/)

add_dependencies(
        scan
        antiword
        libwpd
        mobi
)

target_link_directories(scan PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(
        scan
        PUBLIC

        # --start-group/--end-group makes the link order of the static archives irrelevant
        -Wl,--start-group

        cjson
        ${LibArchive_LIBRARIES}
        ZLIB::ZLIB
        BZip2::BZip2
        lz4::lz4
        ${LZO2_LIB}
        LibLZMA::LibLZMA

        ${MUPDF_LIB}
        openjp2

        ${WPD_LIB_DIR}/libwpd-0.9.a
        ${WPD_LIB_DIR}/libwpd-stream-0.9.a

        ${HARFBUZZ_LIB}
        ${JBIG2DEC_LIB}

        stdc++

        ${JPEG_LIBRARIES}
        ${Tesseract_LIBRARIES}
        ${LIBXML2_LIBRARIES}
        Freetype::Freetype

        z

        ${CMAKE_THREAD_LIBS_INIT}

        ${GOMP_LIB}
        ${CMS_LIB}
        ${JAS_LIB}
        ${GUMBO_LIB}

        antiword
        mobi
        unofficial::pcre::pcre unofficial::pcre::pcre16 unofficial::pcre::pcre32 unofficial::pcre::pcrecpp
        leptonica
        libraw::raw
        ${FFMPEG_LIBRARIES}

        -Wl,--end-group
)

target_include_directories(
        scan
        PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${MUPDF_INC_DIR}
        ${JPEG_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIR}
        ${MOBI_INCLUDE_DIR}
        ${WPD_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
)

target_include_directories(
        sist2 PUBLIC
        ${CMAKE_SOURCE_DIR}/third-party/utf8.h/
        ${CMAKE_SOURCE_DIR}/
)

add_dependencies(
        sist2
        scan
        argparse
)

target_link_libraries(
        sist2

        ZLIB::ZLIB
        libgit2::libgit2package
        argparse
        unofficial::mongoose::mongoose
        CURL::libcurl

        pthread

        scan

        ${MAGIC_LIB}
        unofficial::sqlite3::sqlite3
        OpenBLAS::OpenBLAS
)

if (SIST_STATIC)
    target_link_options(sist2 PRIVATE -static)
endif ()

if (BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)

    add_executable(scan_ub_test tests/libscan/main.cpp tests/libscan/test_util.cpp tests/libscan/test_util.h)
    target_compile_options(scan_ub_test PRIVATE -g -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_libraries(scan_ub_test PRIVATE GTest::gtest GTest::gtest_main -fsanitize=undefined scan)

    add_executable(scan_a_test tests/libscan/main.cpp tests/libscan/test_util.cpp tests/libscan/test_util.h)
    target_compile_options(scan_a_test PRIVATE -g -fsanitize=address -fno-omit-frame-pointer)
    target_link_libraries(scan_a_test PRIVATE GTest::gtest GTest::gtest_main -fsanitize=address scan)

    add_executable(scan_test tests/libscan/main.cpp tests/libscan/test_util.cpp tests/libscan/test_util.h)
    target_compile_options(scan_test PRIVATE -g -fno-omit-frame-pointer)
    target_link_libraries(scan_test PRIVATE GTest::gtest GTest::gtest_main scan)
endif ()

add_custom_target(
        before_sist2
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/before_build.sh
)

add_dependencies(sist2 before_sist2)
