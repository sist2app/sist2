#include "tests/support/scan_fixture.h"

#include <string>

extern "C" {
#include "libscan/sub_document.h"
}

/** The name a sub-document ends up with, and where its extension starts in it */
static void submit(const char *container, const char *name, std::string *path, std::string *ext) {
    vfile_t f = {};
    strcpy(f.filepath, container);
    f.log = noop_log;
    f.logf = noop_logf;

    parse_job_t *job = sub_document_job_create(&f, container, noop_log, noop_logf);

    static std::string out_path;
    static std::string out_ext;

    sub_document_submit([](parse_job_t *sub_job) {
        out_path = sub_job->filepath;
        out_ext = sub_job->filepath + sub_job->ext;
    }, &f, job, name, "data", 4);

    *path = out_path;
    *ext = out_ext;

    free(job);
}

TEST(SubDocumentTest, ExtensionOfANameWithADot) {
    std::string path, ext;
    submit("/mail.pst", "Inbox/message.eml", &path, &ext);

    ASSERT_EQ(path, "/mail.pst#/Inbox/message.eml");
    ASSERT_EQ(ext, "eml");
}

/* A sub-document deeper than one level carries the extension of its parents in its path, and
 * none of those is its own */
TEST(SubDocumentTest, ExtensionOfANameWithoutADot) {
    std::string path, ext;
    submit("/mail.pst", "Inbox/message.eml#/attachment-0", &path, &ext);

    ASSERT_EQ(path, "/mail.pst#/Inbox/message.eml#/attachment-0");
    ASSERT_EQ(ext, "");
}

TEST(SubDocumentTest, ExtensionOfANestedName) {
    std::string path, ext;
    submit("/mail.pst", "Inbox/message.eml#/report.docx", &path, &ext);

    ASSERT_EQ(ext, "docx");
}

/** A dot in a folder name is not an extension of the document inside it */
TEST(SubDocumentTest, DotInAParentComponent) {
    std::string path, ext;
    submit("/mail.pst", "v1.2/message", &path, &ext);

    ASSERT_EQ(ext, "");
}

TEST(SubDocumentTest, Depth) {
    ASSERT_EQ(sub_document_depth("/mail.pst"), 0);
    ASSERT_EQ(sub_document_depth("/mail.pst#/message.eml"), 1);
    ASSERT_EQ(sub_document_depth("/mail.pst#/message.eml#/attachment-0"), 2);
}

/** A name is a single component of the path, so anything in it that would nest it is replaced */
TEST(SubDocumentTest, SanitizeName) {
    char buf[32];

    sub_document_sanitize_name("../etc/passwd", buf, sizeof(buf));
    ASSERT_STREQ(buf, ".._etc_passwd");

    sub_document_sanitize_name("a\nb", buf, sizeof(buf));
    ASSERT_STREQ(buf, "a_b");
}
