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

        // An archive, so that documents nested inside one are covered too
        fs::copy(fs::path(SIST2_TEST_FILES_DIR) / "arc" / "test1.zip", dir / "files" / "test1.zip");
    }

    void TearDown() override {
        fs::remove_all(dir);
    }

    static void write_file(const fs::path &path) {
        std::ofstream file(path);
        file << "the quick brown fox jumps over the lazy dog\n";
    }

    int scan(const std::string &env, const int threads = 4, const std::string &extra_args = "",
             const std::string &args_before_path = "") {
        const std::string command = env + " " + SIST2_BINARY + " scan --threads " + std::to_string(threads)
                              + " " + extra_args
                              + " -o " + index.string()
                              + " " + args_before_path + " " + (dir / "files").string()
                              + " > /dev/null 2>&1";

        const int status = system(command.c_str());

        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    int count(const std::string &where) {
        sqlite3 *db;
        if (sqlite3_open(index.c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, ("SELECT count(*) FROM document WHERE " + where).c_str(), -1, &stmt, nullptr);

        const int result = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return result;
    }

    std::string checksum(const std::string &path) {
        sqlite3 *db;
        if (sqlite3_open(index.c_str(), &db) != SQLITE_OK) {
            return "";
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT json_data ->> 'checksum' FROM document WHERE path = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

        std::string result;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            result = (const char *) sqlite3_column_text(stmt, 0);
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return result;
    }

    /** Only the text files created by SetUp(), so that the archive and its members do not count */
    int text_file_count() {
        return count("parent IS NULL AND path LIKE '%.txt'");
    }

    int document_count() {
        sqlite3 *db;
        if (sqlite3_open(index.c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT count(*) FROM document", -1, &stmt, nullptr);

        const int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return count;
    }
};

TEST_F(ScanMasterTest, IndexesEveryFile) {
    ASSERT_EQ(scan(""), 0);
    ASSERT_EQ(text_file_count(), FILE_COUNT + 1);
}

/** A file deleted between the walk and the parse does not take the scan down with it */
TEST_F(ScanMasterTest, SurvivesAFileDeletedDuringTheScan) {
    ASSERT_EQ(scan("SIST2_DELETE_ON_FILE=file-3.txt"), 0);

    // The walk already had its name, size and mime, so it is indexed with no content
    ASSERT_EQ(text_file_count(), FILE_COUNT + 1);
    ASSERT_EQ(count("path LIKE '%file-3.txt' AND json_data ->> 'content' IS NULL"), 1);
}

/** The same file with nothing to guess its type from: mime detection is what reads it */
TEST_F(ScanMasterTest, SurvivesAFileWithNoExtensionDeletedDuringTheScan) {
    write_file(dir / "files" / "no-extension");

    ASSERT_EQ(scan("SIST2_DELETE_ON_FILE=no-extension"), 0);

    ASSERT_EQ(text_file_count(), FILE_COUNT + 1);
    ASSERT_EQ(count("path LIKE '%no-extension'"), 0);
}

TEST_F(ScanMasterTest, SurvivesACrashingWorker) {
    // Every worker segfaults on this one file, the way a parser would on a malformed document
    ASSERT_EQ(scan(std::string("SIST2_CRASH_ON_FILE=") + CRASH_TRIGGER), 0);

    // The scan still finishes, and only the file that killed the worker is missing
    ASSERT_EQ(text_file_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, SurvivesACrashingWorkerWithASingleWorker) {
    // With one worker there is no second process to fall back on: the master has to respawn it
    ASSERT_EQ(scan(std::string("SIST2_CRASH_ON_FILE=") + CRASH_TRIGGER, 1), 0);

    ASSERT_EQ(text_file_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, SurvivesAWorkerThatExitsCleanlyWithAJobInHand) {
    // A worker leaving with status 0 still lost the file it was holding
    ASSERT_EQ(scan(std::string("SIST2_EXIT_ON_FILE=") + CRASH_TRIGGER, 1), 0);

    ASSERT_EQ(text_file_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, KillsAWorkerThatRunsPastItsDeadline) {
    // Without --job-timeout this scan would never finish
    ASSERT_EQ(scan(std::string("SIST2_HANG_ON_FILE=") + CRASH_TRIGGER, 4, "--job-timeout 1"), 0);

    ASSERT_EQ(text_file_count(), FILE_COUNT);
}

TEST_F(ScanMasterTest, IndexesEveryFileWithAnEndOfOptionsMarker) {
    // Appended after the user's "--", --worker is a positional and the child scans alone
    ASSERT_EQ(scan("", 4, "", "--"), 0);
    ASSERT_EQ(text_file_count(), FILE_COUNT + 1);
}

TEST_F(ScanMasterTest, IncrementalScanKeepsModifiedFiles) {
    ASSERT_EQ(scan(""), 0);

    const int documents = document_count();

    // A re-written row must not be mistaken for one the scan never visited
    const fs::path modified = dir / "files" / "file-0.txt";
    write_file(modified);
    fs::last_write_time(modified, fs::file_time_type::clock::now() + std::chrono::hours(1));

    ASSERT_EQ(scan("", 4, "--incremental"), 0);

    ASSERT_EQ(document_count(), documents);
    ASSERT_EQ(count("path LIKE '%file-0.txt'"), 1);
}

TEST_F(ScanMasterTest, IncrementalScanKeepsUnchangedArchiveMembers) {
    ASSERT_EQ(scan(""), 0);

    const int documents = document_count();
    const int archive_members = count("parent IS NOT NULL");
    ASSERT_GT(archive_members, 0);

    // Nothing changed, so nothing is re-parsed. The members of the archive are never visited
    // individually and have to survive on the strength of their parent being unchanged.
    ASSERT_EQ(scan("", 4, "--incremental"), 0);

    ASSERT_EQ(count("parent IS NOT NULL"), archive_members);
    ASSERT_EQ(document_count(), documents);

    // And again, to make sure the second pass is stable too
    ASSERT_EQ(scan("", 4, "--incremental"), 0);
    ASSERT_EQ(document_count(), documents);
}

/**
 * An unchanged file is skipped before anything opens it, so a file that cannot be read right now —
 * permissions, a lock, a network share having a bad day — keeps its place in the index instead of
 * being taken for a file that is gone.
 */
TEST_F(ScanMasterTest, IncrementalScanKeepsAnUnreadableUnchangedFile) {
    // Without an extension to go on, the media type comes from the content, which is the only case
    // where anything opens a file the scan is about to skip
    const fs::path unreadable = dir / "files" / "no-extension";
    write_file(unreadable);

    ASSERT_EQ(scan(""), 0);

    const int documents = document_count();
    ASSERT_EQ(count("path LIKE '%no-extension'"), 1);

    fs::permissions(unreadable, fs::perms::none);

    ASSERT_EQ(scan("", 4, "--incremental"), 0);

    EXPECT_EQ(document_count(), documents);
    EXPECT_EQ(count("path LIKE '%no-extension'"), 1);

    // TearDown() has to be able to delete it
    fs::permissions(unreadable, fs::perms::owner_read | fs::perms::owner_write);
}

/**
 * The same bytes must hash the same however they were read: straight off the disk, after libmagic
 * peeked at the head, or out of an archive.
 */
TEST_F(ScanMasterTest, ChecksumDoesNotDependOnHowTheFileWasRead) {
    const std::string content = "the gzip member content marker\n";

    std::ofstream(dir / "files" / "on-disk.txt") << content;
    // No known extension, so the mime type comes from libmagic reading the head first
    std::ofstream(dir / "files" / "on-disk.qqq") << content;
    fs::copy(fs::path(SIST2_TEST_FILES_DIR) / "arc" / "text.txt.gz", dir / "files" / "text.txt.gz");

    ASSERT_EQ(scan("", 4, "--checksums"), 0);

    const std::string expected = checksum("on-disk.txt");

    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(checksum("on-disk.qqq"), expected);
    ASSERT_EQ(checksum("text.txt.gz#/text.txt"), expected);
}
