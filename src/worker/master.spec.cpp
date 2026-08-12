#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <sqlite3.h>

/*
 * End-to-end tests of the scan master: they run the real `sist2 scan` binary, which spawns real
 * worker processes, and check what ended up in the index.
 */

namespace fs = std::filesystem;

static const int FILE_COUNT = 24;
static const char *CRASH_TRIGGER = "please-crash";

class ScanMasterTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path index;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("sist2-master-spec-" + std::to_string(getpid()));
        index = dir / "index.sist2";

        fs::remove_all(dir);
        fs::create_directories(dir / "files");

        for (int i = 0; i < FILE_COUNT; i++) {
            write_file(dir / "files" / ("file-" + std::to_string(i) + ".txt"));
        }
        write_file(dir / "files" / (std::string(CRASH_TRIGGER) + ".txt"));
    }

    void TearDown() override {
        fs::remove_all(dir);
    }

    static void write_file(const fs::path &path) {
        std::ofstream file(path);
        file << "the quick brown fox jumps over the lazy dog\n";
    }

    int scan(const std::string &env, int threads = 4, const std::string &extra_args = "") {
        std::string command = env + " " + SIST2_BINARY + " scan --threads " + std::to_string(threads)
                              + " " + extra_args
                              + " -o " + index.string() + " " + (dir / "files").string()
                              + " > /dev/null 2>&1";

        int status = system(command.c_str());

        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    int document_count() {
        sqlite3 *db;
        if (sqlite3_open(index.c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT count(*) FROM document", -1, &stmt, nullptr);

        int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return count;
    }
};

TEST_F(ScanMasterTest, IndexesEveryFile) {
    ASSERT_EQ(scan(""), 0);
    ASSERT_EQ(document_count(), FILE_COUNT + 1);
}

TEST_F(ScanMasterTest, SurvivesACrashingWorker) {
    // Every worker segfaults on this one file, the way a parser would on a malformed document
    ASSERT_EQ(scan(std::string("SIST2_CRASH_ON_FILE=") + CRASH_TRIGGER), 0);

    // The scan still finishes, and only the file that killed the worker is missing
    ASSERT_EQ(document_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, SurvivesACrashingWorkerWithASingleWorker) {
    // With one worker there is no second process to fall back on: the master has to respawn it
    ASSERT_EQ(scan(std::string("SIST2_CRASH_ON_FILE=") + CRASH_TRIGGER, 1), 0);

    ASSERT_EQ(document_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, KillsAWorkerThatRunsPastItsDeadline) {
    // Without --job-timeout this scan would never finish
    ASSERT_EQ(scan(std::string("SIST2_HANG_ON_FILE=") + CRASH_TRIGGER, 4, "--job-timeout 1"), 0);

    ASSERT_EQ(document_count(), FILE_COUNT);
}
