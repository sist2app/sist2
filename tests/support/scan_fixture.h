#ifndef SIST2_TESTS_SCAN_FIXTURE_H
#define SIST2_TESTS_SCAN_FIXTURE_H

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "libscan/scan.h"

#include "libscan/arc/arc.h"
#include "libscan/comic/comic.h"
#include "libscan/ebook/ebook.h"
#include "libscan/email/email.h"
#include "libscan/font/font.h"
#include "libscan/json/json.h"
#include "libscan/media/media.h"
#include "libscan/mobi/scan_mobi.h"
#include "libscan/msdoc/msdoc.h"
#include "libscan/ooxml/ooxml.h"
#include "libscan/raw/raw.h"
#include "libscan/text/text.h"
#include "libscan/wpd/wpd.h"
}

/** Set by CMake to the absolute path of the libscan test-files corpus */
#ifndef SIST2_TEST_FILES_DIR
#define SIST2_TEST_FILES_DIR "third-party/libscan-test-files/test_files"
#endif

/** Absolute path of a corpus file, e.g. test_file("text/books.csv") */
std::string test_file(const std::string &relative_path);

/** Every regular file in the corpus, as paths relative to the corpus root */
std::vector<std::string> corpus_files();

/**
 * Folder containing eng.traineddata, looked up once in $TESSDATA_PREFIX, ./tessdata, then the
 * usual distro locations. The OCR code paths fail without it.
 */
const char *tessdata_path();

/** Whether <lang>.traineddata is where tessdata_path() points; OCR of that language needs it */
bool tesseract_has_language(const char *lang);

void noop_log(const char *filepath, int level, char *str);

void noop_logf(const char *filepath, int level, char *format, ...);

/*
 * Parser context factories. Every context is returned by value, so tests own theirs and can
 * mutate it freely without affecting any other test.
 */
scan_text_ctx_t make_text_ctx(long content_size = 500);

scan_ebook_ctx_t make_ebook_ctx(long content_size = 500, int fast_epub_parse = FALSE);

scan_comic_ctx_t make_comic_ctx(int tn_size = 500, int enable_tn = TRUE);

scan_media_ctx_t make_media_ctx(int tn_size = 500, int tn_count = 1);

scan_ooxml_ctx_t make_ooxml_ctx(long content_size = 500, int enable_tn = TRUE);

scan_mobi_ctx_t make_mobi_ctx(long content_size = 500);

scan_raw_ctx_t make_raw_ctx(int tn_size = 500, int enable_tn = TRUE);

scan_msdoc_ctx_t make_msdoc_ctx(long content_size = 500);

scan_wpd_ctx_t make_wpd_ctx(long content_size = 500);

scan_json_ctx_t make_json_ctx(long content_size = 5000);

scan_font_ctx_t make_font_ctx(int enable_tn = TRUE);

/**
 * Archive context. Sub-documents found while recursing are handed to the parser installed with
 * ArcTest::recurse_into(); ARC_MODE_LIST and ARC_MODE_SKIP need no parser.
 */
scan_arc_ctx_t make_arc_ctx(archive_mode_t mode, const char *passphrase = nullptr);

/**
 * Email context. Attachments and mbox messages are handed to the parser installed with
 * ArcTest::recurse_into(), the same way archive entries are.
 */
scan_email_ctx_t make_email_ctx(long content_size = 5000);

/* Document helpers */

meta_line_t *get_meta(const document_t *doc, metakey key);

meta_line_t *get_meta_from(meta_line_t *meta, metakey key);

/** Total byte size of all the thumbnails stored on the document */
size_t thumbnail_size(const document_t *doc);

/** Number of MetaThumbnail meta lines stored on the document */
int thumbnail_meta_count(const document_t *doc);

void destroy_doc(document_t *doc);

/** Overwrite n random runs of width bytes; each run has a trunc_p% chance of truncating instead */
void fuzz_buffer(char *buf, size_t *buf_len, int width, int n, int trunc_p);

/**
 * Owns a document_t and its vfile_t for the duration of one test, so no test has to remember to
 * free anything — teardown runs even when an ASSERT_* aborts the test body early.
 */
class ScanTest : public ::testing::Test {
protected:
    vfile_t f = {};
    document_t doc = {};

    /** Open a corpus file, relative to the corpus root. Fails the test if it is missing. */
    void load(const std::string &relative_path);

    /** Open any file by path, for tests that write the file they need */
    void load_path(const std::string &path);

    /** Read from memory instead of the filesystem. The buffer must outlive the test body. */
    void load_mem(const void *buf, size_t len);

    /** Free the current document and file, then start over with empty ones */
    void reset();

    void TearDown() override;

    meta_line_t *meta(metakey key) const { return get_meta(&doc, key); }

    /** MetaContent as a string; fails the test (rather than crashing) when there is none */
    const char *content() const;

    size_t content_len() const { return strlen(content()); }

    size_t thumbnails_size() const { return thumbnail_size(&doc); }

    int thumbnails_count() const { return thumbnail_meta_count(&doc); }
};

/** Documents produced by recursing into an archive. Owns them; frees them at teardown. */
class SubDocCollector {
public:
    ~SubDocCollector();

    document_t *create();

    size_t size() const { return docs_.size(); }

    /** Sub-document at index, in the order the archive yielded them */
    document_t *at(size_t index) const;

    document_t *last() const;

private:
    std::vector<document_t *> docs_;
};

/** Called for each sub-document of an archive; doc is zeroed and owned by the collector. */
using SubDocParser = std::function<void(parse_job_t *job, document_t *doc)>;

/**
 * Base fixture for anything that recurses into an archive. The parser installed by recurse_into()
 * runs for every entry, and the resulting documents stay available in sub_docs.
 */
class ArcTest : public ScanTest {
protected:
    SubDocCollector sub_docs;

    void recurse_into(SubDocParser parser);

    void TearDown() override;
};

#endif
