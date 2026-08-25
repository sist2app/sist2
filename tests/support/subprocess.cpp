#include "subprocess.h"

#include <cstdlib>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sist2::test {

    const char *quiet() {
#ifdef _WIN32
        return " > nul 2>&1";
#else
        return " > /dev/null 2>&1";
#endif
    }

    namespace {
        void set_variable(const std::string &name, const std::string &value) {
#ifdef _WIN32
            _putenv_s(name.c_str(), value.c_str());
#else
            setenv(name.c_str(), value.c_str(), 1);
#endif
        }

        void clear_variable(const std::string &name) {
#ifdef _WIN32
            // An empty value is how the CRT removes a variable
            _putenv_s(name.c_str(), "");
#else
            unsetenv(name.c_str());
#endif
        }
    }

    void set_test_env(const std::string &name, const std::string &value) {
        set_variable(name, value);
    }

    void unset_test_env(const std::string &name) {
        clear_variable(name);
    }

    int make_pipe(int fds[2]) {
#ifdef _WIN32
        // 4096-byte buffer, binary mode: the frame protocol is not text
        return _pipe(fds, 4096, _O_BINARY);
#else
        return pipe(fds);
#endif
    }

    int run(const std::string &command, const std::string &env) {
        // The variable goes into this process rather than onto the command line: system() passes
        // the environment down, and cmd.exe has no per-command assignment syntax to write it in
        const size_t equals = env.find('=');
        std::string name;
        std::string previous_value;
        bool was_set = false;

        if (equals != std::string::npos) {
            name = env.substr(0, equals);
            const char *previous = std::getenv(name.c_str());
            was_set = previous != nullptr;
            if (was_set) {
                previous_value = previous;
            }
            set_variable(name, env.substr(equals + 1));
        }

        const int status = system(command.c_str());

        if (equals != std::string::npos) {
            if (was_set) {
                set_variable(name, previous_value);
            } else {
                clear_variable(name);
            }
        }

#ifdef _WIN32
        // system() reports the child's exit code directly
        return status;
#else
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    }
}
