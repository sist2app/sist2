#include "tests/support/scan_fixture.h"

#include <fstream>
#include <string>

/*
 * The corpus carries no Outlook mailbox: a .pst can only be written by Outlook itself, so there is
 * nothing to generate one with. Everything that needs a real mailbox reads whatever .pst or .ost
 * is in test_files/pst/, and is skipped when there is none.
 */
static std::string pst_fixture() {
    for (const std::string &path: corpus_files()) {
        if (path.rfind("pst/", 0) != 0) {
            continue;
        }

        const size_t dot = path.rfind('.');
        if (dot != std::string::npos &&
            (path.substr(dot) == ".pst" || path.substr(dot) == ".ost")) {
            return path;
        }
    }

    return "";
}

/* GTEST_SKIP() only returns from the function it is written in, so it belongs in the test body */
#define LOAD_FIXTURE_OR_SKIP()                                                  \
    const std::string fixture = pst_fixture();                                  \
    if (fixture.empty()) {                                                      \
        GTEST_SKIP() << "No Outlook mailbox in test_files/pst/";                \
    }                                                                           \
    load(fixture)

class PstTest : public ArcTest {
protected:
    scan_pst_ctx_t ctx = make_pst_ctx();

    /** Collects the messages and attachments as documents, with their path and nothing else */
    void collect_sub_docs() {
        recurse_into([](parse_job_t *job, document_t *sub_doc) {
            strcpy(sub_doc->filepath, job->filepath);
            strcpy(sub_doc->parent, job->parent);
        });
    }

    static size_t count_occurrences(const std::string &haystack, const std::string &needle) {
        size_t count = 0;

        for (size_t pos = haystack.find(needle); pos != std::string::npos;
             pos = haystack.find(needle, pos + needle.size())) {
            count += 1;
        }

        return count;
    }
};

/** Every message is a document of its own, named after the folder it is in and its subject */
TEST_F(PstTest, MessagesAreSubDocuments) {
    collect_sub_docs();
    LOAD_FIXTURE_OR_SKIP();

    parse_pst(&ctx, &f, &doc);

    ASSERT_GT(sub_docs.size(), 0);

    bool found_message = false;
    for (size_t i = 0; i < sub_docs.size(); i++) {
        const std::string path = sub_docs.at(i)->filepath;

        if (count_occurrences(path, "#/") == 1) {
            ASSERT_EQ(path.rfind(".eml"), path.size() - 4);
            found_message = true;
        }
    }

    ASSERT_TRUE(found_message);
}

/** A message is written back out as a mail message, and read as one */
TEST_F(PstTest, MessagesAreParsedAsEmail) {
    scan_email_ctx_t email_ctx = make_email_ctx();

    recurse_into([&email_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        if (std::string(job->filepath).rfind(".eml") == std::string(job->filepath).size() - 4) {
            parse_email(&email_ctx, &job->vfile, sub_doc);
        }
    });

    LOAD_FIXTURE_OR_SKIP();

    parse_pst(&ctx, &f, &doc);

    bool found_title = false;
    for (size_t i = 0; i < sub_docs.size(); i++) {
        if (get_meta(sub_docs.at(i), MetaTitle) != nullptr) {
            found_title = true;
        }
    }

    ASSERT_TRUE(found_title);
}

/** An attachment is a document of its own, parented to the message it came with */
TEST_F(PstTest, AttachmentsAreParentedToTheirMessage) {
    collect_sub_docs();
    LOAD_FIXTURE_OR_SKIP();

    parse_pst(&ctx, &f, &doc);

    for (size_t i = 0; i < sub_docs.size(); i++) {
        const std::string path = sub_docs.at(i)->filepath;

        if (count_occurrences(path, "#/") < 2) {
            continue;
        }

        // "<mailbox>#/<folder>/<subject>.eml#/<filename>"
        const std::string parent = path.substr(0, path.rfind("#/"));

        ASSERT_EQ(std::string(sub_docs.at(i)->parent), parent);
        ASSERT_EQ(parent.rfind(".eml"), parent.size() - 4);
        return;
    }

    GTEST_SKIP() << "No attachment in " << pst_fixture();
}

/** The media type of a mailbox is the one libmagic reports for a .msg item as well */
TEST_F(PstTest, NotAPffFileIsIgnored) {
    collect_sub_docs();

    const std::string path = std::string(testing::TempDir()) + "/sist2-not-a-mailbox.msg";
    std::ofstream(path, std::ios::binary) << std::string(4096, 'x');

    load_path(path);

    ASSERT_EQ(parse_pst(&ctx, &f, &doc), SCAN_OK);
    ASSERT_EQ(sub_docs.size(), 0);

    remove(path.c_str());
}

/** A mailbox reached through an archive is read through a stream, and copied to disk to be opened */
TEST_F(PstTest, ReadFromMemory) {
    const std::string fixture = pst_fixture();

    if (fixture.empty()) {
        GTEST_SKIP() << "No Outlook mailbox in test_files/pst/";
    }

    std::ifstream file(test_file(fixture), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    collect_sub_docs();
    load_mem(bytes.data(), bytes.size());

    // load_mem() stands in for a file on disk; a mailbox found in an archive has no path
    f.is_fs_file = FALSE;

    ASSERT_EQ(parse_pst(&ctx, &f, &doc), SCAN_OK);
    ASSERT_GT(sub_docs.size(), 0);
}
