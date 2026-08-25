#include <gtest/gtest.h>

#include "tests/support/subprocess.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "tests/support/temp_path.h"

extern "C" {
#include "src/ignorelist.h"
#include "src/ctx.h"
}

/**
 * ignorelist_is_ignored() matches rules against the path relative to the scan root, which it reads
 * from the global ScanCtx.
 */
class IgnorelistTest : public ::testing::Test {
protected:
    std::string root = temp_path("ignorelist-root");
    std::string ignore_file = root + "/.sist2ignore";
    ignorelist_t *ignorelist = nullptr;
    std::string original_tmpdir;
    bool had_tmpdir = false;

    void SetUp() override {
        std::filesystem::create_directories(root);

        // ignorelist_create() puts its scratch git repository in $TMPDIR
        const char *tmpdir = getenv("TMPDIR");
        had_tmpdir = tmpdir != nullptr;
        if (had_tmpdir) {
            original_tmpdir = tmpdir;
        }
        sist2::test::set_test_env("TMPDIR", root);

        ScanCtx.index.desc.root_len = (int) (root.size() + 1);
        ignorelist = ignorelist_create();
    }

    void TearDown() override {
        ignorelist_destroy(ignorelist);
        std::filesystem::remove_all(root);

        // Left pointing at the deleted directory, temp_directory_path() throws in
        // every later suite.
        if (had_tmpdir) {
            sist2::test::set_test_env("TMPDIR", original_tmpdir);
        } else {
            sist2::test::unset_test_env("TMPDIR");
        }
    }

    void write_rules(const std::string &rules) {
        std::ofstream file(ignore_file);
        file << rules;
        file.close();

        ignorelist_load_ignore_file(ignorelist, ignore_file.c_str());
    }

    int is_ignored(const std::string &relative_path) {
        return ignorelist_is_ignored(ignorelist, (root + "/" + relative_path).c_str());
    }
};

TEST_F(IgnorelistTest, NothingIsIgnoredWithoutRules) {
    ASSERT_FALSE(is_ignored("file.txt"));
    ASSERT_FALSE(is_ignored("folder/file.log"));
}

TEST_F(IgnorelistTest, MissingIgnoreFileIsNotAnError) {
    ignorelist_load_ignore_file(ignorelist, "/tmp/sist2-no-such-ignore-file");

    ASSERT_FALSE(is_ignored("file.txt"));
}

TEST_F(IgnorelistTest, ExtensionRule) {
    write_rules("*.log\n");

    ASSERT_TRUE(is_ignored("file.log"));
    ASSERT_TRUE(is_ignored("folder/file.log"));
    ASSERT_FALSE(is_ignored("file.txt"));
}

TEST_F(IgnorelistTest, FolderRule) {
    write_rules("node_modules/\n");

    ASSERT_TRUE(is_ignored("node_modules/package/index.js"));
    ASSERT_FALSE(is_ignored("src/index.js"));
}

TEST_F(IgnorelistTest, NegatedRule) {
    write_rules("*.log\n!keep.log\n");

    ASSERT_TRUE(is_ignored("file.log"));
    ASSERT_FALSE(is_ignored("keep.log"));
}

TEST_F(IgnorelistTest, RootAnchoredRule) {
    write_rules("/build\n");

    ASSERT_TRUE(is_ignored("build"));
    ASSERT_FALSE(is_ignored("src/build"));
}
