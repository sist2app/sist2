#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sqlite3.h>

/*
 * End-to-end tests of the search index build: they run the real `sist2 scan` and
 * `sist2 sqlite-index` binaries and check what a query against the resulting index returns.
 */

namespace fs = std::filesystem;

class SqliteIndexTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path index;
    fs::path search_index;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("sist2-fts-spec-" + std::to_string(getpid()));
        index = dir / "index.sist2";
        search_index = dir / "search.sist2";

        fs::remove_all(dir);
        fs::create_directories(dir / "files");

        for (int i = 0; i < 8; i++) {
            write_file("file-" + std::to_string(i) + ".txt", "alpha zebra document number " + std::to_string(i));
        }
    }

    void TearDown() override {
        fs::remove_all(dir);
    }

    void write_file(const std::string &name, const std::string &content) {
        std::ofstream file(dir / "files" / name);
        file << content << "\n";
        file.close();

        // Modified files are picked up by mtime, and a test writes them within the same second
        fs::last_write_time(dir / "files" / name,
                            fs::file_time_type::clock::now() + std::chrono::hours(24 * ++mtime_offset));
    }

    int scan(const std::string &extra_args = "") {
        return run(std::string(SIST2_BINARY) + " scan --threads 4 " + extra_args
                   + " -o " + index.string() + " " + (dir / "files").string());
    }

    int sqlite_index(const std::string &extra_args = "") {
        return run(std::string(SIST2_BINARY) + " sqlite-index --search-index " + search_index.string()
                   + " " + extra_args + " " + index.string());
    }

    static int run(const std::string &command) {
        const int status = system((command + " > /dev/null 2>&1").c_str());
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    long long query(const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(search_index.c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return -1;
        }

        const long long result = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int64(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return result;
    }

    bool exec(const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(search_index.c_str(), &db) != SQLITE_OK) {
            return false;
        }

        const bool ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(db);

        return ok;
    }

    long long matches(const std::string &term) {
        return query("SELECT count(*) FROM search WHERE search MATCH '" + term + "'");
    }

    // Returns false if fts5 finds the search index inconsistent with its content table
    bool integrity_ok() {
        return exec("INSERT INTO search(search) VALUES('integrity-check')");
    }

    int mtime_offset = 0;
};

TEST_F(SqliteIndexTest, FullBuild) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, IncrementalUpdate) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    write_file("file-0.txt", "alpha replaced with omega");
    write_file("new-file.txt", "brandnew content");
    fs::remove(dir / "files" / "file-1.txt");

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index(), 0);

    // The updated document keeps the terms it still has, loses the ones it does not
    EXPECT_EQ(matches("alpha"), 7);
    EXPECT_EQ(matches("omega"), 1);
    EXPECT_EQ(matches("zebra"), 6);
    EXPECT_EQ(matches("brandnew"), 1);

    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    EXPECT_EQ(query("SELECT documents FROM index_state"), 8);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, IncrementalMatchesRebuild) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    write_file("file-0.txt", "alpha replaced with omega");
    fs::remove(dir / "files" / "file-1.txt");

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<std::string> terms = {"alpha", "omega", "zebra", "document"};
    std::vector<long long> incremental;
    for (const auto &term: terms) {
        incremental.push_back(matches(term));
    }

    ASSERT_EQ(sqlite_index("--rebuild"), 0);

    for (size_t i = 0; i < terms.size(); i++) {
        EXPECT_EQ(matches(terms[i]), incremental[i]) << "term: " << terms[i];
    }
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, RunWithNoChangesIsANoop) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, InterruptedRunIsRebuilt) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    // What a run that died halfway through leaves behind: the search index cannot be trusted, so the
    // next run has to rebuild it instead of applying changes on top
    ASSERT_TRUE(exec("UPDATE index_state SET dirty = 1;"
                     "INSERT INTO search(search, rowid, name, content, title, path)"
                     " SELECT 'delete', id, name, content, title, path FROM document_view;"));
    ASSERT_EQ(matches("alpha"), 0);

    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_TRUE(integrity_ok());
}
