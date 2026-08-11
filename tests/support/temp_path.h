#ifndef SIST2_TESTS_TEMP_PATH_H
#define SIST2_TESTS_TEMP_PATH_H

#include <string>
#include <unistd.h>

/**
 * Scratch path for a test, unique to this process. ctest runs the plain, ASan and UBSan binaries
 * concurrently, so a fixed path would have them writing over each other.
 */
inline std::string temp_path(const std::string &name) {
    return "/tmp/sist2-test-" + std::to_string(getpid()) + "-" + name;
}

#endif
