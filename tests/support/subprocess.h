#ifndef SIST2_TESTS_SUBPROCESS_H
#define SIST2_TESTS_SUBPROCESS_H

#include <string>

namespace sist2::test {
    /** Redirection that silences both streams, in the syntax of the platform's shell */
    const char *quiet();

    /**
     * Runs `command` through the shell and returns the exit status, or -1 when the process did
     * not exit normally. `env` is a single NAME=VALUE assignment applied to the command only (the
     * value may contain spaces); any previous value is restored afterwards.
     */
    int run(const std::string &command, const std::string &env = "");

    /** setenv(), which Windows spells differently */
    void set_test_env(const std::string &name, const std::string &value);

    /** unsetenv(), which Windows spells differently */
    void unset_test_env(const std::string &name);

    /** An anonymous pipe as a pair of file descriptors. Returns 0 on success. */
    int make_pipe(int fds[2]);
}

#endif
