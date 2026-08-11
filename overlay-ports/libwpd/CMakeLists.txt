# Replacement build for the vcpkg port: static libraries + install rules only.
# Upstream ships autotools; the two libraries and their sources mirror src/lib/Makefile.am.
cmake_minimum_required(VERSION 3.15)

project(libwpd CXX)

set(CMAKE_CXX_STANDARD 11)

find_package(ZLIB REQUIRED)

set(WPD_STREAM_SOURCES
        src/lib/WPXStreamImplementation.cpp
        src/lib/WPXOLEStream.cpp
        src/lib/WPXZipStream.cpp
)

file(GLOB WPD_SOURCES "src/lib/*.cpp")
foreach (source ${WPD_STREAM_SOURCES})
    list(REMOVE_ITEM WPD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
endforeach ()

add_library(wpd STATIC ${WPD_SOURCES})
target_include_directories(wpd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>)

add_library(wpd-stream STATIC ${WPD_STREAM_SOURCES})
target_include_directories(wpd-stream PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>)
# BUILD_ZIP_STREAM is what configure defines when zlib is available
target_compile_definitions(wpd-stream PRIVATE BUILD_ZIP_STREAM)
target_link_libraries(wpd-stream PUBLIC ZLIB::ZLIB)

install(
        TARGETS wpd wpd-stream
        ARCHIVE DESTINATION lib
)

install(
        DIRECTORY inc/libwpd inc/libwpd-stream
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
)
