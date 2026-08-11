# Replacement build for the vcpkg port: static library + install rules only.
# Option/definition handling mirrors upstream CMakeLists.txt (v0.11), with
# sist2's configuration as the defaults (no xmlwriter/libxml2).
cmake_minimum_required(VERSION 3.12)

project(libmobi C)

set(CMAKE_C_STANDARD 99)

option(USE_ENCRYPTION "Enable encryption" ON)
option(USE_LIBXML2 "Use libxml2 instead of internal xmlwriter" OFF)
option(USE_XMLWRITER "Enable xmlwriter (for opf support)" OFF)

set(PACKAGE_VERSION "0.11" CACHE STRING "libmobi version")
add_definitions(-DPACKAGE_VERSION="${PACKAGE_VERSION}")

if(USE_ENCRYPTION)
    add_definitions(-DUSE_ENCRYPTION)
endif()

if(USE_XMLWRITER)
    add_definitions(-DUSE_XMLWRITER)
    if(USE_LIBXML2)
        add_definitions(-DUSE_LIBXML2)
        find_package(LibXml2 REQUIRED)
    endif()
endif()

find_package(ZLIB REQUIRED)

include(CheckIncludeFile)
include(CheckFunctionExists)
check_include_file(unistd.h HAVE_UNISTD_H)
if(HAVE_UNISTD_H)
    add_definitions(-DHAVE_UNISTD_H)
endif()
check_function_exists(strdup HAVE_STRDUP)
if(HAVE_STRDUP)
    add_definitions(-DHAVE_STRDUP)
endif()
check_include_file(sys/resource.h HAVE_SYS_RESOURCE_H)
if(HAVE_SYS_RESOURCE_H)
    add_definitions(-DHAVE_SYS_RESOURCE_H)
endif()

include(CheckCSourceCompiles)
foreach(keyword "inline" "__inline__" "__inline")
    check_c_source_compiles("${keyword} void func(); void func() { } int main() { func(); return 0; }" HAVE_INLINE)
    if(HAVE_INLINE)
        add_definitions(-DMOBI_INLINE=${keyword})
        break()
    endif()
endforeach()

check_c_source_compiles("void func() { } __attribute__((noreturn)); int main() { func(); return 0; }" HAVE_ATTRIBUTE_NORETURN)
if(HAVE_ATTRIBUTE_NORETURN)
    add_definitions(-DHAVE_ATTRIBUTE_NORETURN)
endif()

set(mobi_SOURCES
    src/buffer.c
    src/compression.c
    src/debug.c
    src/index.c
    src/memory.c
    src/meta.c
    src/parse_rawml.c
    src/read.c
    src/structure.c
    src/util.c
    src/write.c
    src/xmlwriter.c
)

if(USE_ENCRYPTION)
    list(APPEND mobi_SOURCES src/encryption.c src/sha1.c src/randombytes.c)
endif()

if(USE_XMLWRITER)
    list(APPEND mobi_SOURCES src/opf.c)
endif()

add_library(mobi STATIC ${mobi_SOURCES})

set_target_properties(mobi PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_link_libraries(mobi PUBLIC ZLIB::ZLIB)
if(USE_XMLWRITER AND USE_LIBXML2)
    target_link_libraries(mobi PUBLIC LibXml2::LibXml2)
endif()

install(TARGETS mobi ARCHIVE DESTINATION lib)
install(FILES src/mobi.h DESTINATION include)
