cmake_minimum_required(VERSION 3.15)

project(sist2)
set(CMAKE_C_STANDARD 11)

option(SIST_DEBUG "Build a debug executable" on)
option(SIST_FAST "Enable more optimisation flags" off)
option(SIST_DEBUG_INFO "Turn on debug information in web interface" on)
option(SIST_STATIC "Produce a fully static executable (musl only)" off)
option(BUILD_TESTS "Build the test binaries" on)
option(SIST_COVERAGE "Instrument libscan and sist2_core for gcov coverage" off)

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

#
# libscan — file parsing library (text/metadata/thumbnail extraction)
#
add_library(
        scan
        libscan/util.h
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
)
set_target_properties(scan PROPERTIES LINKER_LANGUAGE C)

#
# Generated sources — written to the build tree only, never into the source tree
#
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

add_executable(mime_gen scripts/mime_gen.c)

add_custom_command(
        OUTPUT ${GENERATED_DIR}/mime_generated.c
        COMMAND mime_gen ${CMAKE_SOURCE_DIR}/scripts/mime.csv ${GENERATED_DIR}/mime_generated.c
        DEPENDS mime_gen ${CMAKE_SOURCE_DIR}/scripts/mime.csv
        COMMENT "Generating mime_generated.c"
)

# Runs every build; git_hash.cmake only touches the header when the hash changed
add_custom_target(
        git_hash
        COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_FILE=${GENERATED_DIR}/git_hash.h
        -P ${CMAKE_SOURCE_DIR}/scripts/git_hash.cmake
        BYPRODUCTS ${GENERATED_DIR}/git_hash.h
)

#
# Everything but main() lives in sist2_core, so the test binaries can link against it
#
add_library(
        sist2_core STATIC
        # argparse
        third-party/argparse/argparse.h third-party/argparse/argparse.c

        src/sist.h
        src/io/walk.h src/io/walk.c
        src/parsing/parse.h src/parsing/parse.c
        src/parsing/magic_util.c src/parsing/magic_util.h
        src/io/serialize.h src/io/serialize.c
        src/parsing/mime.h src/parsing/mime.c ${GENERATED_DIR}/mime_generated.c
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
        src/ignorelist.h

        src/worker/queue.c src/worker/queue.h
        src/worker/thread_pool.c src/worker/thread_pool.h
        src/worker/protocol.c src/worker/protocol.h
        src/worker/sink.c src/worker/sink.h
        src/worker/worker.c src/worker/worker.h
        src/worker/master.c src/worker/master.h)
set_target_properties(sist2_core PROPERTIES LINKER_LANGUAGE C)

add_executable(sist2 src/main.c)
set_target_properties(sist2 PROPERTIES LINKER_LANGUAGE C)

target_link_directories(sist2_core PUBLIC BEFORE ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/)

find_package(PkgConfig REQUIRED)

find_package(cJSON CONFIG REQUIRED)
find_package(unofficial-mongoose CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_library(MAGIC_LIB NAMES libmagic.a REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenBLAS CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

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

find_library(MUPDF_LIB NAMES libmupdf.a REQUIRED)
find_library(MOBI_LIB NAMES libmobi.a mobi REQUIRED)
find_library(ANTIWORD_LIB NAMES libantiword.a antiword REQUIRED)
find_path(MOBI_INCLUDE_DIR mobi.h REQUIRED)
find_path(ANTIWORD_INCLUDE_DIR antiword/antiword.h REQUIRED)
find_path(UTF8H_INCLUDE_DIR utf8h/utf8.h REQUIRED)
find_library(CMS_LIB NAMES lcms2)
find_library(JAS_LIB NAMES jasper)
find_library(GUMBO_LIB NAMES gumbo)
find_library(WPD_LIB NAMES wpd REQUIRED)
find_library(WPD_STREAM_LIB NAMES wpd-stream REQUIRED)
find_path(WPD_INCLUDE_DIR libwpd/libwpd.h REQUIRED)
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
# -Wmissing-field-initializers excluded: omitted fields are guaranteed zero-initialized,
# and the vendored argparse OPT_*() variadic macros cannot pad trailing fields
set(SIST_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
)
target_compile_options(scan PRIVATE ${SIST_WARNING_OPTIONS})
target_compile_options(sist2_core PRIVATE ${SIST_WARNING_OPTIONS})
target_compile_options(sist2 PRIVATE ${SIST_WARNING_OPTIONS})

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
    target_compile_options(sist2_core PRIVATE ${SIST_SANITIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_SANITIZE_OPTIONS})

    # PUBLIC: sist2_core carries ASan-instrumented objects, so everything linking it — the test
    # binaries included — has to link the runtime as well
    target_link_options(sist2_core PUBLIC -fsanitize=address)
    target_link_options(
            sist2
            PRIVATE
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
    target_compile_options(sist2_core PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_OPTIMIZE_OPTIONS})
else ()
    target_compile_definitions(scan PRIVATE NDEBUG)
    set(SIST_OPTIMIZE_OPTIONS
            -Ofast
            -fno-stack-protector
            -fomit-frame-pointer
    )
    target_compile_options(scan PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2_core PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_OPTIMIZE_OPTIONS})
endif ()

target_compile_options(
        scan
        PRIVATE
        -g
)

target_link_directories(scan PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(
        scan
        PUBLIC

        # libmobi bundles its own SHA1_* implementation, which collides with libcrypto's. Pulling
        # libcrypto's sha1 object in first (by path, so CMake can't reorder it behind libmobi)
        # means libmobi's is never extracted.
        ${OPENSSL_CRYPTO_LIBRARY}

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

        ${WPD_LIB}
        ${WPD_STREAM_LIB}

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

        ${ANTIWORD_LIB}
        ${MOBI_LIB}
        unofficial::pcre::pcre unofficial::pcre::pcre16 unofficial::pcre::pcre32 unofficial::pcre::pcrecpp
        leptonica
        libraw::raw
        ${FFMPEG_LIBRARIES}

        -Wl,--end-group

        # tesseract (inside the group) needs curl and BLAS, so they come after it
        CURL::libcurl
        OpenBLAS::OpenBLAS
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
        ${ANTIWORD_INCLUDE_DIR}
        ${UTF8H_INCLUDE_DIR}
        ${WPD_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
)

target_include_directories(
        sist2_core PUBLIC
        ${CMAKE_SOURCE_DIR}/
        ${GENERATED_DIR}
)

add_dependencies(
        sist2_core
        scan
        git_hash
)

#
# Embedded assets (src/embed.h .incbin — absolute paths passed as macros)
#
set(WEB_ASSETS
        sist2-vue/src/assets/favicon.ico
        sist2-vue/dist/index.html
        sist2-vue/dist/js/index.js
        sist2-vue/dist/js/chunk-vendors.js
        sist2-vue/dist/css/index.css
)
foreach (ASSET ${WEB_ASSETS})
    if (NOT EXISTS ${CMAKE_SOURCE_DIR}/${ASSET})
        message(FATAL_ERROR "${ASSET} not found. Build the web frontend first: (cd sist2-vue && npm install && npm run build)")
    endif ()
endforeach ()
list(TRANSFORM WEB_ASSETS PREPEND ${CMAKE_SOURCE_DIR}/)

set(SCHEMA_ASSETS
        ${CMAKE_SOURCE_DIR}/schema/mappings.json
        ${CMAKE_SOURCE_DIR}/schema/settings.json
        ${CMAKE_SOURCE_DIR}/schema/settings_legacy.json
        ${CMAKE_SOURCE_DIR}/schema/pipeline.json
)

if (NOT MAGIC_MGC AND DEFINED ENV{MAGIC_MGC})
    set(MAGIC_MGC $ENV{MAGIC_MGC})
endif ()
if (NOT MAGIC_MGC)
    find_file(
            MAGIC_MGC magic.mgc
            PATHS
            ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/libmagic/misc
            /vcpkg/installed/x64-linux/share/libmagic/misc
            /work/vcpkg/installed/x64-linux/share/libmagic/misc
            /usr/lib/file
    )
endif ()
if (NOT MAGIC_MGC OR NOT EXISTS ${MAGIC_MGC})
    message(FATAL_ERROR "magic.mgc not found; pass -DMAGIC_MGC=/path/to/magic.mgc or set the MAGIC_MGC environment variable")
endif ()

target_compile_definitions(
        sist2_core PRIVATE
        SIST2_ROOT="${CMAKE_SOURCE_DIR}"
        MAGIC_MGC_PATH="${MAGIC_MGC}"
)

# The compiler emits no depfile entries for .incbin'd files — declare them here
set_source_files_properties(src/web/web_util.c PROPERTIES OBJECT_DEPENDS "${WEB_ASSETS}")
set_source_files_properties(src/index/elastic.c PROPERTIES OBJECT_DEPENDS "${SCHEMA_ASSETS}")
set_source_files_properties(src/parsing/magic_util.c PROPERTIES OBJECT_DEPENDS "${MAGIC_MGC}")

target_link_libraries(
        sist2_core
        PUBLIC

        ZLIB::ZLIB
        libgit2::libgit2package
        unofficial::mongoose::mongoose
        CURL::libcurl

        pthread

        scan

        ${MAGIC_LIB}
        unofficial::sqlite3::sqlite3
        OpenBLAS::OpenBLAS
        libuv::uv_a
)

target_link_libraries(sist2 PRIVATE sist2_core)

if (SIST_STATIC)
    target_link_options(sist2 PRIVATE -static)
endif ()

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

#
# Coverage: -DSIST_COVERAGE=on, then `make coverage` (needs gcovr)
#
if (SIST_COVERAGE)
    target_compile_options(scan PRIVATE --coverage)
    target_link_options(scan PUBLIC --coverage)
    target_compile_options(sist2_core PRIVATE --coverage)
    target_link_options(sist2_core PUBLIC --coverage)

    add_custom_target(
            coverage
            COMMAND gcovr --root ${CMAKE_SOURCE_DIR}
            --filter ${CMAKE_SOURCE_DIR}/libscan --filter ${CMAKE_SOURCE_DIR}/src
            --exclude ".*\\.spec\\.cpp" --exclude ".*/tests/.*"
            --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
            --print-summary
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Collecting coverage into ${CMAKE_BINARY_DIR}/coverage/index.html"
    )
endif ()
