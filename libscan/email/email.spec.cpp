#include "tests/support/scan_fixture.h"

#include <string>

class EmailTest : public ArcTest {
protected:
    scan_email_ctx_t ctx = make_email_ctx();

    /** Collects the attachments as documents, with their path and nothing else */
    void collect_sub_docs() {
        recurse_into([](parse_job_t *job, document_t *sub_doc) {
            strcpy(sub_doc->filepath, job->filepath);
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

TEST_F(EmailTest, SimpleHeaders) {
    load("email/simple.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Notes from the Tuesday review");
    // The text buffer keeps only the characters a search runs on; '<' and '@' are not among them
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Alice Martin alice example.org");
}

/** The headers a mailbox is searched by are indexed along with the body */
TEST_F(EmailTest, SimpleContent) {
    load("email/simple.eml");

    parse_email(&ctx, &f, &doc);

    const std::string text = content();

    ASSERT_NE(text.find("bob example.com"), std::string::npos);
    ASSERT_NE(text.find("archive example.com"), std::string::npos);
    ASSERT_NE(text.find("The review went through every open item"), std::string::npos);
}

/** quoted-printable, then the charset it declares */
TEST_F(EmailTest, QuotedPrintableIsDecoded) {
    load("email/simple.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_NE(std::string(content()).find("café"), std::string::npos);
}

TEST_F(EmailTest, SimpleHasNoAttachment) {
    collect_sub_docs();
    load("email/simple.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_EQ(sub_docs.size(), 0);
}

TEST_F(EmailTest, EncodedWordSubject) {
    load("email/multipart.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Quarterly numbers — final draft");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Sofia Araújo sofia example.org");
}

/** Both branches of a multipart/alternative say the same thing; only one of them is indexed */
TEST_F(EmailTest, AlternativeIsNotIndexedTwice) {
    load("email/multipart.eml");

    parse_email(&ctx, &f, &doc);

    const std::string text = content();

    ASSERT_EQ(count_occurrences(text, "the quarterly numbers is attached"), 1);
    ASSERT_NE(text.find("with the largest jump"), std::string::npos);
}

TEST_F(EmailTest, AttachmentsAreSubDocuments) {
    collect_sub_docs();
    load("email/multipart.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_EQ(sub_docs.size(), 2);
    ASSERT_STREQ(sub_docs.at(0)->filepath,
                 (test_file("email/multipart.eml") + "#/revenue.csv").c_str());
    ASSERT_STREQ(sub_docs.at(1)->filepath,
                 (test_file("email/multipart.eml") + "#/pixel.png").c_str());
}

/** An attachment is handed to the parser as a file of its own, decoded */
TEST_F(EmailTest, AttachmentIsDecoded) {
    scan_text_ctx_t text_ctx = make_text_ctx();

    recurse_into([&text_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    load("email/multipart.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_NE(std::string(get_meta(sub_docs.at(0), MetaContent)->str_val).find("year,quarter,region,revenue"),
              std::string::npos);
}

TEST_F(EmailTest, MboxSplitsMessages) {
    collect_sub_docs();
    load("email/mailbox.mbox");

    parse_mbox(&ctx, &f, &doc);

    ASSERT_EQ(sub_docs.size(), 4);
    ASSERT_STREQ(sub_docs.at(0)->filepath,
                 (test_file("email/mailbox.mbox") + "#/message-0.eml").c_str());
    ASSERT_STREQ(sub_docs.at(3)->filepath,
                 (test_file("email/mailbox.mbox") + "#/message-3.eml").c_str());
}

/** Each message of a mailbox goes back through the message parser */
TEST_F(EmailTest, MboxMessagesAreParsed) {
    scan_email_ctx_t message_ctx = make_email_ctx();

    recurse_into([&message_ctx](parse_job_t *job, document_t *sub_doc) {
        parse_email(&message_ctx, &job->vfile, sub_doc);
    });

    load("email/mailbox.mbox");

    parse_mbox(&ctx, &f, &doc);

    ASSERT_EQ(sub_docs.size(), 4);
    ASSERT_STREQ(get_meta(sub_docs.at(0), MetaTitle)->str_val, "First message in the mailbox");
    ASSERT_STREQ(get_meta(sub_docs.at(3), MetaTitle)->str_val, "Fourth message in the mailbox");
    ASSERT_NE(std::string(get_meta(sub_docs.at(0), MetaContent)->str_val).find("aardvark"),
              std::string::npos);
}

/** A "From " line inside a body is escaped by the mbox writer, and unescaped on the way out */
TEST_F(EmailTest, MboxUnquotesFromLine) {
    scan_email_ctx_t message_ctx = make_email_ctx();

    recurse_into([&message_ctx](parse_job_t *job, document_t *sub_doc) {
        parse_email(&message_ctx, &job->vfile, sub_doc);
    });

    load("email/mailbox.mbox");

    parse_mbox(&ctx, &f, &doc);

    const std::string text = get_meta(sub_docs.at(0), MetaContent)->str_val;

    ASSERT_NE(text.find("From the desk of nobody"), std::string::npos);
    ASSERT_EQ(text.find(">From the desk of nobody"), std::string::npos);
}

TEST_F(EmailTest, ContentSizeIsRespected) {
    ctx = make_email_ctx(64);

    load("email/simple.eml");

    parse_email(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 64, 4);
}
