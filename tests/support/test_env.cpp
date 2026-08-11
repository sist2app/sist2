#include <gtest/gtest.h>

#include <clocale>

extern "C" {
#include <libavutil/avutil.h>
}

#ifdef __SANITIZE_ADDRESS__
/**
 * antiword allocates its document state with xmalloc() and never frees it when it bails out on a
 * malformed file, which the msdoc fuzz test does a thousand times over. Nothing we can free from here.
 */
extern "C" const char *__lsan_default_suppressions() {
    return "leak:xmalloc\n";
}
#endif

namespace {
    /** Process-wide setup shared by every test binary */
    class Sist2TestEnvironment : public ::testing::Environment {
    public:
        void SetUp() override {
            setlocale(LC_ALL, "");
            av_log_set_level(AV_LOG_QUIET);
        }
    };

    [[maybe_unused]] const auto *registered = ::testing::AddGlobalTestEnvironment(new Sist2TestEnvironment);
}
