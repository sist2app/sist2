#include "tests/support/scan_fixture.h"

#include <fstream>
#include <string>

class PstTest : public ArcTest {
protected:
    scan_pst_ctx_t ctx = make_pst_ctx();

    /** Collects the messages and attachments as documents, with their path and parent only */
    void collect_sub_docs() {
        recurse_into([](parse_job_t *job, document_t *sub_doc) {
            strcpy(sub_doc->filepath, job->filepath);
            strcpy(sub_doc->parent, job->parent);
        });
    }

    /** Index of the sub-document at that path, or -1 */
    int index_of(const std::string &path) const {
        for (size_t i = 0; i < sub_docs.size(); i++) {
            if (path == sub_docs.at(i)->filepath) {
                return (int) i;
            }
        }

        return -1;
    }

    static int ends_with(const std::string &path, const std::string &suffix) {
        return path.size() > suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
};

/** Every message is a document of its own, named after the folder it is in and its subject */
TEST_F(PstTest, MessagesAreSubDocuments) {
    collect_sub_docs();
    load("pst/various_body_types.pst");

    // The dispatcher writes the mailbox itself first, and the messages are parented to it
    strcpy(doc.filepath, f.filepath);

    ASSERT_EQ(parse_pst(&ctx, &f, &doc), SCAN_OK);

    ASSERT_EQ(sub_docs.size(), 4);
    ASSERT_STREQ(sub_docs.at(0)->filepath,
                 (test_file("pst/various_body_types.pst") +
                  "#/Top of Outlook data file/Inbox/tmp/original email (2097188).eml").c_str());
    ASSERT_STREQ(sub_docs.at(0)->parent, test_file("pst/various_body_types.pst").c_str());
}

/** A message is written back out as a mail message, and read as one */
TEST_F(PstTest, MessagesAreParsedAsEmail) {
    scan_email_ctx_t email_ctx = make_email_ctx();

    recurse_into([&email_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        parse_email(&email_ctx, &job->vfile, sub_doc);
    });

    load("pst/various_body_types.pst");

    parse_pst(&ctx, &f, &doc);

    ASSERT_EQ(sub_docs.size(), 4);
    ASSERT_STREQ(get_meta(sub_docs.at(0), MetaTitle)->str_val, "original email");
    // The text buffer keeps only the characters a search runs on; '<' and '@' are not among them
    ASSERT_STREQ(get_meta(sub_docs.at(0), MetaAuthor)->str_val,
                 "Allison, Timothy B. tallison mitre.org");

    const std::string text = get_meta(sub_docs.at(0), MetaContent)->str_val;

    // The headers a mailbox is searched by are indexed along with the body
    ASSERT_NE(text.find("Subject: original email"), std::string::npos);
    ASSERT_NE(text.find("Date: Wed, 30 Aug 2017 19:26:04 +0000"), std::string::npos);
    ASSERT_NE(text.find("This is the original email"), std::string::npos);
}

/** The time the message was received, rather than the time the mailbox was last written */
TEST_F(PstTest, MessagesKeepTheirOwnDate) {
    recurse_into([](parse_job_t *job, document_t *sub_doc) {
        sub_doc->mtime = job->vfile.mtime;
    });

    load("pst/various_body_types.pst");

    parse_pst(&ctx, &f, &doc);

    // 2017-08-30T19:26:04Z, the delivery time of the first message
    ASSERT_EQ(sub_docs.at(0)->mtime, 1504121164);
    ASSERT_NE(sub_docs.at(0)->mtime, f.mtime);
}

TEST_F(PstTest, FolderNamesOutsideAsciiAreKept) {
    collect_sub_docs();
    load("pst/mailbox.pst");

    parse_pst(&ctx, &f, &doc);

    ASSERT_NE(index_of(test_file("pst/mailbox.pst") +
                       "#/Début du fichier de données Outlook/Feature Generators (2097252).eml"),
              -1);
}

/** A message attached to a message is a document below it, and so is what that one carries */
TEST_F(PstTest, AttachedMessagesAreParentedToTheirMessage) {
    collect_sub_docs();
    load("pst/mailbox.pst");

    parse_pst(&ctx, &f, &doc);

    const std::string message =
            test_file("pst/mailbox.pst") +
            "#/Début du fichier de données Outlook/FW: First email (2097380).eml";
    const std::string attached_message = message + "#/First email (2097412).eml";
    const std::string attachment = attached_message + "#/attachment.docx";

    ASSERT_NE(index_of(message), -1);
    ASSERT_NE(index_of(attached_message), -1);
    ASSERT_NE(index_of(attachment), -1);

    ASSERT_STREQ(sub_docs.at(index_of(attached_message))->parent, message.c_str());
    ASSERT_STREQ(sub_docs.at(index_of(attachment))->parent, attached_message.c_str());
}

/** An attachment is handed to the parser as a file of its own */
TEST_F(PstTest, AttachmentIsParsed) {
    scan_ooxml_ctx_t ooxml_ctx = make_ooxml_ctx();

    recurse_into([&ooxml_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);

        if (ends_with(job->filepath, ".docx")) {
            parse_ooxml(&ooxml_ctx, &job->vfile, sub_doc);
        }
    });

    load("pst/mailbox.pst");

    parse_pst(&ctx, &f, &doc);

    const int attachment = index_of(
            test_file("pst/mailbox.pst") +
            "#/Début du fichier de données Outlook/FW: First email (2097380).eml"
            "#/First email (2097412).eml#/attachment.docx");

    ASSERT_NE(attachment, -1);
    ASSERT_STREQ(get_meta(sub_docs.at(attachment), MetaContent)->str_val,
                 "This is a docx attachment.");
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
    std::ifstream file(test_file("pst/various_body_types.pst"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    collect_sub_docs();
    load_mem(bytes.data(), bytes.size());

    // load_mem() stands in for a file on disk; a mailbox found in an archive has no path
    f.is_fs_file = FALSE;

    ASSERT_EQ(parse_pst(&ctx, &f, &doc), SCAN_OK);
    ASSERT_EQ(sub_docs.size(), 4);
}
