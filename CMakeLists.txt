cmake_minimum_required(VERSION 3.19)

option(SIST_DEBUG "Build a debug executable" on)
option(SIST_FAST "Enable more optimisation flags" off)
option(SIST_DEBUG_INFO "Turn on debug information in web interface" on)
option(SIST_STATIC "Link libgcc, libstdc++ and the thread runtime statically (musl and mingw)" off)
option(BUILD_TESTS "Build the test binaries" on)
option(SIST_COVERAGE "Instrument libscan and sist2_core for gcov coverage" off)

# Both of these have to be decided before project(), which is what runs the vcpkg toolchain:
# VCPKG_BUILD_TYPE picks which half of every port gets built, and the toolchain searches
# debug/lib first whenever CMAKE_BUILD_TYPE is Debug *or unset* — leaving the build type unset
# links the optimized binary against -O0 mupdf, freetype and harfbuzz, which costs ~1.8x on PDF
# parsing and leaves MuPDF's assert()s live in production.
if (SIST_DEBUG)
    set(VCPKG_BUILD_TYPE debug)
    set(SIST_DEFAULT_BUILD_TYPE Debug)
else ()
    set(VCPKG_BUILD_TYPE release)
    set(SIST_DEFAULT_BUILD_TYPE Release)
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE ${SIST_DEFAULT_BUILD_TYPE} CACHE STRING "Build type" FORCE)
endif ()

project(sist2)
set(CMAKE_C_STANDARD 11)

# mingw ships no sanitizer runtimes for Windows targets, so the debug build cannot be produced
# there. Saying so here beats a link failure several hundred object files later.
if (WIN32 AND SIST_DEBUG)
    message(FATAL_ERROR "SIST_DEBUG is not supported for the Windows build: mingw has no ASan. Configure with -DSIST_DEBUG=off")
endif ()

add_compile_definitions(
        # Before any system header is included, or glibc hides asprintf & co.
        _GNU_SOURCE
        "SIST_PLATFORM=${SIST_PLATFORM}"
)

if (WIN32)
    # find_library() defaults to preferring an import library over the static archive, and
    # pkg-config resolution goes through it: ffmpeg's -lpthread came back as libpthread.dll.a and
    # dragged libwinpthread-1.dll into an otherwise self-contained binary. (The warning against
    # doing this further down applies to the glibc build, where it makes -lm resolve to a static
    # libm.a that cannot link. mingw has no such libm.)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll.a")

    # msvcrt's printf understands neither %zu nor %lld, both of which sist2 uses throughout.
    # This selects mingw's own conformant implementation, and with it gcc's gnu_printf checking.
    # _FILE_OFFSET_BITS makes struct stat, off_t and lseek() 64-bit, which they are not by
    # default on Windows: a file over 2GB would otherwise be indexed with a truncated size.
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1 _FILE_OFFSET_BITS=64)
endif ()

if (SIST_DEBUG)
    add_compile_definitions(
            "SIST_DEBUG=${SIST_DEBUG}"
    )
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
        libscan/sub_document.c libscan/sub_document.h
        libscan/macros.h
        libscan/win32.h libscan/win32.c

        libscan/text/text.c libscan/text/text.h
        libscan/arc/arc.c libscan/arc/arc.h
        libscan/ebook/ebook.c libscan/ebook/ebook.h
        libscan/comic/comic.c libscan/comic/comic.h
        libscan/ooxml/ooxml.c libscan/ooxml/ooxml.h
        libscan/media/media.c libscan/media/media.h
        libscan/font/font.c libscan/font/font.h
        libscan/msdoc/msdoc.c libscan/msdoc/msdoc.h
        libscan/email/email.c libscan/email/email.h
        libscan/pst/pst.c libscan/pst/pst.h
        libscan/pst/rtf.c libscan/pst/rtf.h
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

# mime_gen runs during the build, so a cross build cannot use the target compiler for it. It is a
# single freestanding C file with no dependencies, which makes compiling it by hand cheaper than
# a second CMake project.
if (CMAKE_CROSSCOMPILING)
    find_program(HOST_C_COMPILER NAMES cc gcc clang REQUIRED)
    set(MIME_GEN ${GENERATED_DIR}/mime_gen)

    add_custom_command(
            OUTPUT ${MIME_GEN}
            COMMAND ${HOST_C_COMPILER} -O2 -o ${MIME_GEN} ${CMAKE_SOURCE_DIR}/scripts/mime_gen.c
            DEPENDS ${CMAKE_SOURCE_DIR}/scripts/mime_gen.c
            COMMENT "Building mime_gen for the build host"
    )
else ()
    add_executable(mime_gen scripts/mime_gen.c)
    set(MIME_GEN $<TARGET_FILE:mime_gen>)
endif ()

add_custom_command(
        OUTPUT ${GENERATED_DIR}/mime_generated.c
        COMMAND ${MIME_GEN} ${CMAKE_SOURCE_DIR}/scripts/mime.csv ${GENERATED_DIR}/mime_generated.c
        DEPENDS ${MIME_GEN} ${CMAKE_SOURCE_DIR}/scripts/mime.csv
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
        src/web/highlight.c src/web/highlight.h
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
        src/database/database_fts_fuzzy.c
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

if (WIN32)
    enable_language(RC)
    # Carries the application manifest: UTF-8 paths and long path support
    add_executable(sist2 src/main.c src/sist2.rc)
else ()
    add_executable(sist2 src/main.c)
endif ()
set_target_properties(sist2 PROPERTIES LINKER_LANGUAGE C)

target_link_directories(sist2_core PUBLIC BEFORE ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/)

# The libraries found by path below (mupdf, ffmpeg, libmagic) bring no import library
# dependencies of their own, and Windows keeps sockets, crypto and the shell out of the CRT.
if (WIN32)
    set(SIST_WIN32_LIBRARIES
            ws2_32 wsock32 bcrypt crypt32 secur32 ncrypt iphlpapi userenv
            ole32 oleaut32 uuid shlwapi shell32 advapi32 user32 gdi32
            winmm psapi dbghelp version wldap32 normaliz mfplat mfuuid strmiids
    )
endif ()

find_package(PkgConfig REQUIRED)

find_package(cJSON CONFIG REQUIRED)
find_package(unofficial-mongoose CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_library(MAGIC_LIB NAMES libmagic.a REQUIRED)
# On Windows, libmagic is patched to use libtre for its regexes instead of the system's
if (WIN32)
    find_library(TRE_LIB NAMES tre REQUIRED)
endif ()
find_package(unofficial-sqlite3 CONFIG REQUIRED)

#
# spellfix1, the fuzzy-search vocabulary. The sqlite3 port builds the amalgamation, which does not
# carry ext/misc/, so the extension is vendored and compiled in: SQLITE_CORE links it statically
# and src/database/database.c registers the module by hand instead of loading it at runtime.
#
add_library(sqlite_spellfix STATIC third-party/sqlite-spellfix/spellfix.c)
target_compile_definitions(sqlite_spellfix PRIVATE SQLITE_CORE)
target_link_libraries(sqlite_spellfix PUBLIC unofficial::sqlite3::sqlite3)
find_package(OpenBLAS CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

find_package(LibArchive REQUIRED)
find_package(BZip2 REQUIRED)
find_package(lz4 REQUIRED)

find_package(Threads REQUIRED)

if (WIN32)
    # Threads resolves to winpthread's import library, and -static cannot override an explicit
    # -l because an import library is an archive too. -l: names the static archive by filename.
    set(SIST_PTHREAD_LIBRARY -l:libpthread.a)

    # Same for the shared libgcc that CMake adds implicitly for the C++ objects
    list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES gcc_s)
    list(REMOVE_ITEM CMAKE_CXX_IMPLICIT_LINK_LIBRARIES gcc_s)
else ()
    set(SIST_PTHREAD_LIBRARY ${CMAKE_THREAD_LIBS_INIT})
endif ()
find_package(Tesseract CONFIG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(unofficial-pcre CONFIG REQUIRED)

# NAMES_PER_DIR: try every spelling in one directory before moving on, so the debug/release
# search order the toolchain set up decides which one wins, not the order of the names
find_library(JBIG2DEC_LIB NAMES jbig2dec jbig2decd NAMES_PER_DIR)
find_library(HARFBUZZ_LIB NAMES harfbuzz harfbuzzd NAMES_PER_DIR)
find_library(LZO2_LIB NAMES lzo2)

find_library(MUPDF_LIB NAMES libmupdf.a REQUIRED)
find_library(MOBI_LIB NAMES libmobi.a mobi REQUIRED)
find_library(ANTIWORD2_LIB NAMES libantiword2.a antiword2 REQUIRED)
find_library(ENVELOPE_LIB NAMES libenvelope.a envelope REQUIRED)
find_library(PFF_LIB NAMES libpff.a pff REQUIRED)
find_path(MOBI_INCLUDE_DIR mobi.h REQUIRED)
find_path(ANTIWORD2_INCLUDE_DIR antiword2.h REQUIRED)
find_path(ENVELOPE_INCLUDE_DIR envelope.h REQUIRED)
find_path(PFF_INCLUDE_DIR libpff.h REQUIRED)
find_path(UTF8H_INCLUDE_DIR utf8h/utf8.h REQUIRED)
find_library(CMS_LIB NAMES lcms2)
find_library(JAS_LIB NAMES jasper)
find_library(GUMBO_LIB NAMES gumbo)
find_library(WPD_LIB NAMES wpd REQUIRED)
find_library(WPD_STREAM_LIB NAMES wpd-stream REQUIRED)
find_path(WPD_INCLUDE_DIR libwpd/libwpd.h REQUIRED)
# openblas is built with OpenMP threading, so the static archive has to come along. Asking the
# compiler beats guessing: the path holds its version, and it answers for whatever gcc is installed.
execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgomp.a
        OUTPUT_VARIABLE GOMP_LIB
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT IS_ABSOLUTE "${GOMP_LIB}")
    find_library(GOMP_LIB NAMES libgomp.a gomp REQUIRED)
endif ()

find_package(Leptonica CONFIG REQUIRED)
find_package(libraw CONFIG REQUIRED)
find_package(Freetype REQUIRED)

find_package(FFMPEG REQUIRED)
list(REMOVE_ITEM FFMPEG_LIBRARIES /usr/lib/x86_64-linux-gnu/libm.a)
list(REMOVE_ITEM FFMPEG_LIBRARIES /usr/lib/aarch64-linux-gnu/libm.a)

#
# Per-configuration compile flags (shared by scan and sist2)
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

#
# Hardening (optimized builds; the debug build gets -fstack-protector alongside ASan below)
#
# libscan parses files nobody vetted, in a process that keeps running afterwards, so the
# optimized binary is exactly the one that needs the checks. _FORTIFY_SOURCE is undefined
# first because the distro gcc predefines it at -O and would warn about the redefinition.
if (WIN32)
    set(SIST_HARDENING_OPTIONS -fstack-protector-strong)
else ()
    set(SIST_HARDENING_OPTIONS
            -fstack-protector-strong
            -U_FORTIFY_SOURCE
            -D_FORTIFY_SOURCE=2
    )
endif ()

if (SIST_DEBUG)
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
            -fomit-frame-pointer
            -freciprocal-math
            ${SIST_HARDENING_OPTIONS}
    )
    target_compile_options(scan PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2_core PRIVATE ${SIST_OPTIMIZE_OPTIONS})
    target_compile_options(sist2 PRIVATE ${SIST_OPTIMIZE_OPTIONS})
else ()
    target_compile_definitions(scan PRIVATE NDEBUG)
    set(SIST_OPTIMIZE_OPTIONS
            -Ofast
            -fomit-frame-pointer
            ${SIST_HARDENING_OPTIONS}
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

        # The imported target rather than a bare -lz: outside a linux sysroot there is no
        # system zlib for the linker to fall back on
        ZLIB::ZLIB

        ${SIST_PTHREAD_LIBRARY}

        ${GOMP_LIB}
        ${CMS_LIB}
        ${JAS_LIB}
        ${GUMBO_LIB}

        ${ANTIWORD2_LIB}
        ${ENVELOPE_LIB}
        ${MOBI_LIB}
        ${PFF_LIB}
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
        ${ANTIWORD2_INCLUDE_DIR}
        ${ENVELOPE_INCLUDE_DIR}
        ${PFF_INCLUDE_DIR}
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

# magic.mgc is ~10MB of the binary uncompressed; it is embedded gzipped and inflated on
# first use instead, which only the processes that actually do mime detection pay for.
set(MAGIC_MGC_GZ ${GENERATED_DIR}/magic.mgc.gz)
file(SIZE ${MAGIC_MGC} MAGIC_MGC_SIZE)

add_custom_command(
        OUTPUT ${MAGIC_MGC_GZ}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${MAGIC_MGC} -DOUTPUT=${MAGIC_MGC_GZ}
        -P ${CMAKE_SOURCE_DIR}/scripts/gzip_file.cmake
        DEPENDS ${MAGIC_MGC} ${CMAKE_SOURCE_DIR}/scripts/gzip_file.cmake
        COMMENT "Compressing magic.mgc"
)
add_custom_target(magic_mgc_gz DEPENDS ${MAGIC_MGC_GZ})
add_dependencies(sist2_core magic_mgc_gz)

target_compile_definitions(
        sist2_core PRIVATE
        SIST2_ROOT="${CMAKE_SOURCE_DIR}"
        MAGIC_MGC_GZ_PATH="${MAGIC_MGC_GZ}"
        MAGIC_MGC_SIZE=${MAGIC_MGC_SIZE}
)

# The compiler emits no depfile entries for .incbin'd files — declare them here
set_source_files_properties(src/web/web_util.c PROPERTIES OBJECT_DEPENDS "${WEB_ASSETS}")
set_source_files_properties(src/index/elastic.c PROPERTIES OBJECT_DEPENDS "${SCHEMA_ASSETS}")
set_source_files_properties(src/parsing/magic_util.c PROPERTIES OBJECT_DEPENDS "${MAGIC_MGC_GZ}")

target_link_libraries(
        sist2_core
        PUBLIC

        ZLIB::ZLIB
        libgit2::libgit2package
        unofficial::mongoose::mongoose
        CURL::libcurl

        ${SIST_PTHREAD_LIBRARY}

        scan

        ${MAGIC_LIB}
        ${TRE_LIB}
        unofficial::sqlite3::sqlite3
        sqlite_spellfix
        OpenBLAS::OpenBLAS
        libuv::uv_a

        ${SIST_WIN32_LIBRARIES}
)

target_link_libraries(sist2 PRIVATE sist2_core)

if (SIST_STATIC)
    target_link_options(sist2 PRIVATE -static)

    if (WIN32)
        target_link_options(sist2 PRIVATE -static-libgcc -static-libstdc++)
    endif ()

    if (NOT WIN32)
        # musl's mallocng returns every freed block to the kernel: ~594 mmap/munmap pairs
        # per scanned file. mimalloc's archive overrides musl's weak malloc symbols.
        find_package(mimalloc CONFIG REQUIRED)
        target_link_libraries(sist2 PRIVATE mimalloc-static)
    endif ()
endif ()

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# Dependencies pull Threads::Threads in on their own, so the imported target has to carry the
# static archive too, not only the places sist2 links it directly. This runs after every
# find_package that could re-import the target (GTest's config among them), because the property
# is read at generate time and the last write is the one that counts.
if (WIN32 AND TARGET Threads::Threads)
    set_property(TARGET Threads::Threads PROPERTY INTERFACE_LINK_LIBRARIES ${SIST_PTHREAD_LIBRARY})
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
