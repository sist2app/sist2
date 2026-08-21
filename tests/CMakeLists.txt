#
# Test binaries.
#
# Test sources live next to the code they cover and are named *.spec.cpp; cross-cutting suites that
# have no single owning source file live in tests/. Both are globbed with CONFIGURE_DEPENDS, so a new
# spec file is picked up by a plain `make` with no CMake edit.
#
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SCAN_SPEC_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/libscan/*.spec.cpp
        ${CMAKE_SOURCE_DIR}/tests/corpus/*.spec.cpp
)

file(GLOB_RECURSE SIST2_SPEC_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.spec.cpp
)

set(TEST_SUPPORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/support/scan_fixture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/support/scan_fixture.h
        ${CMAKE_CURRENT_SOURCE_DIR}/support/test_env.cpp
)

#
# sist2_add_test(NAME <target> [SANITIZER <name>] SOURCES <files...> LIBRARIES <targets...>)
#
function(sist2_add_test)
    cmake_parse_arguments(T "" "NAME;SANITIZER" "SOURCES;LIBRARIES" ${ARGN})

    if (NOT T_SOURCES)
        return()
    endif ()

    add_executable(${T_NAME} ${T_SOURCES} ${TEST_SUPPORT_SOURCES})

    # Keep the binaries in build/ rather than build/tests/
    set_target_properties(${T_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    target_compile_options(${T_NAME} PRIVATE -g -fno-omit-frame-pointer)
    target_include_directories(${T_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${GENERATED_DIR})
    target_compile_definitions(
            ${T_NAME} PRIVATE
            SIST2_TEST_FILES_DIR="${CMAKE_SOURCE_DIR}/third-party/libscan-test-files/test_files"
    )

    # The scan master specs drive the real binary, which spawns real worker processes
    if (TARGET sist2)
        add_dependencies(${T_NAME} sist2)
        target_compile_definitions(${T_NAME} PRIVATE SIST2_BINARY="$<TARGET_FILE:sist2>")
    endif ()
    target_link_libraries(${T_NAME} PRIVATE GTest::gtest GTest::gmock GTest::gtest_main ${T_LIBRARIES})

    if (T_SANITIZER)
        target_compile_options(${T_NAME} PRIVATE -fsanitize=${T_SANITIZER})
        target_link_options(${T_NAME} PRIVATE -fsanitize=${T_SANITIZER})
    endif ()

    # PRE_TEST: enumerate the tests when ctest runs, not while building
    gtest_discover_tests(${T_NAME} DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
endfunction()

sist2_add_test(NAME scan_test SOURCES ${SCAN_SPEC_SOURCES} LIBRARIES scan)
sist2_add_test(NAME scan_a_test SANITIZER address SOURCES ${SCAN_SPEC_SOURCES} LIBRARIES scan)
sist2_add_test(NAME scan_ub_test SANITIZER undefined SOURCES ${SCAN_SPEC_SOURCES} LIBRARIES scan)

sist2_add_test(NAME sist2_test SOURCES ${SIST2_SPEC_SOURCES} LIBRARIES sist2_core)
sist2_add_test(NAME sist2_a_test SANITIZER address SOURCES ${SIST2_SPEC_SOURCES} LIBRARIES sist2_core)
sist2_add_test(NAME sist2_ub_test SANITIZER undefined SOURCES ${SIST2_SPEC_SOURCES} LIBRARIES sist2_core)
