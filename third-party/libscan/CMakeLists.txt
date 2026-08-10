cmake_minimum_required(VERSION 3.15)

project(scan)
set(CMAKE_C_STANDARD 11)

option(BUILD_TESTS "Build tests" on)

add_subdirectory(third-party/antiword)

set(USE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(USE_XMLWRITER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/libmobi)

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

        third-party/utf8.h
        libscan/mobi/scan_mobi.c libscan/mobi/scan_mobi.h libscan/raw/raw.c libscan/raw/raw.h)
set_target_properties(scan PROPERTIES LINKER_LANGUAGE C)

if (SIST_DEBUG)
    add_compile_definitions(
            antiword
            DEBUG
    )
    target_compile_options(
            antiword
            PRIVATE
            -g
            -fstack-protector
            -fno-omit-frame-pointer
            -fsanitize=address
            -fno-inline
    )
elseif (SIST_FAST)
    add_compile_definitions(
            antiword
            NDEBUG
    )

    target_compile_options(
            scan
            PRIVATE

            -Ofast
            -march=native
            -fno-stack-protector
            -fomit-frame-pointer
            -freciprocal-math
    )
else()
    add_compile_definitions(
            antiword
            NDEBUG
    )

    target_compile_options(
            scan
            PRIVATE

            -Ofast
            #-march=native
            -fno-stack-protector
            -fomit-frame-pointer
            #-freciprocal-math
            -w
    )
endif()


find_package(cJSON CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(BZip2 REQUIRED)
find_package(lz4 REQUIRED)

find_package(Threads REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(ZLIB REQUIRED)
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
SET(WPD_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/third-party/ext_libwpd/src/libwpd/src/lib/.libs/)
SET(WPD_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/third-party/ext_libwpd/src/libwpd/inc/)

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
        ${MUPDF_INC_DIR}
        ${JPEG_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIR}
        ${MOBI_INCLUDE_DIR}
        ${WPD_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
)

if (BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)

    add_executable(scan_ub_test test/main.cpp test/test_util.cpp test/test_util.h)
    target_compile_options(scan_ub_test PRIVATE -g -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_libraries(scan_ub_test PRIVATE GTest::gtest GTest::gtest_main -fsanitize=undefined scan)

    add_executable(scan_a_test test/main.cpp test/test_util.cpp test/test_util.h)
    target_compile_options(scan_a_test PRIVATE -g -fsanitize=address -fno-omit-frame-pointer)
    target_link_libraries(scan_a_test PRIVATE GTest::gtest GTest::gtest_main -fsanitize=address scan)

    add_executable(scan_test test/main.cpp test/test_util.cpp test/test_util.h)
    target_compile_options(scan_test PRIVATE -g -fno-omit-frame-pointer)
    target_link_libraries(scan_test PRIVATE GTest::gtest GTest::gtest_main scan)
endif()
