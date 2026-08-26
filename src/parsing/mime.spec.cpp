#include <gtest/gtest.h>

#include <set>

extern "C" {
#include "src/parsing/mime.h"
}

/*
 * mime ids are generated from scripts/mime.csv at build time and are persisted in .sist2 index
 * files, so they must stay stable across releases: the exact-value assertions below are the
 * regression guard for that.
 */

/**
 * Existing .sist2 files store these numbers, so they may never change. If this test fails, the id
 * assignment scheme in scripts/mime_gen.c changed and every existing index just became unreadable.
 */
TEST(Mime, IdsAreStable) {
    ASSERT_EQ(mime_get_mime_by_string("application/pdf"), 655395u | 0x40000000u);
    ASSERT_EQ(mime_get_mime_by_string("application/zip"), 655601u | 0x10000000u);
    ASSERT_EQ(mime_get_mime_by_string("application/x-font-ttf"), 655490u | 0x20000000u);
    ASSERT_EQ(mime_get_mime_by_string("image/png"), 524585u);
    ASSERT_EQ(mime_get_mime_by_string("text/plain"), 590195u);
}

/**
 * A mime added to the csv carries a '+' and is numbered after every mime that was already there,
 * so that it does not renumber the ones sorted after it.
 */
TEST(Mime, AddedMimesKeepTheOtherIdsStable) {
    ASSERT_EQ(mime_get_mime_by_string("image/avif"), 524741u);
    ASSERT_EQ(mime_get_mime_by_ext("avif"), mime_get_mime_by_string("image/avif"));
}

TEST(Mime, LookupByExtension) {
    ASSERT_EQ(mime_get_mime_by_ext("pdf"), mime_get_mime_by_string("application/pdf"));
    ASSERT_STREQ(mime_get_mime_text(mime_get_mime_by_ext("pdf")), "application/pdf");
}

TEST(Mime, LookupByExtensionIsCaseInsensitive) {
    ASSERT_EQ(mime_get_mime_by_ext("PDF"), mime_get_mime_by_ext("pdf"));
    ASSERT_EQ(mime_get_mime_by_ext("PnG"), mime_get_mime_by_ext("png"));
}

TEST(Mime, UnknownExtension) {
    ASSERT_EQ(mime_get_mime_by_ext("not-an-extension"), 0);
}

TEST(Mime, UnknownMimeString) {
    ASSERT_EQ(mime_get_mime_by_string("application/x-not-a-real-mime"), 0);
}

/** libmagic returns strings such as "[application/pdf]" or " application/pdf" */
TEST(Mime, LeadingBracketsAndSpacesAreIgnored) {
    const unsigned int expected = mime_get_mime_by_string("application/pdf");

    ASSERT_EQ(mime_get_mime_by_string("[application/pdf"), expected);
    ASSERT_EQ(mime_get_mime_by_string("  application/pdf"), expected);
}

TEST(Mime, MajorMimeCategories) {
    ASSERT_EQ(MAJOR_MIME(mime_get_mime_by_string("image/png")), MimeImage);
    ASSERT_EQ(MAJOR_MIME(mime_get_mime_by_string("video/mp4")), MimeVideo);
    ASSERT_EQ(MAJOR_MIME(mime_get_mime_by_string("audio/mpeg")), MimeAudio);
    ASSERT_EQ(MAJOR_MIME(mime_get_mime_by_string("text/plain")), MimeText);
    ASSERT_EQ(MAJOR_MIME(mime_get_mime_by_string("application/pdf")), MimeApplication);
}

TEST(Mime, ParserMasks) {
    ASSERT_TRUE(IS_PDF(mime_get_mime_by_string("application/pdf")));
    ASSERT_TRUE(IS_ARC(mime_get_mime_by_string("application/zip")));
    ASSERT_TRUE(IS_FONT(mime_get_mime_by_string("application/x-font-ttf")));
    ASSERT_TRUE(IS_FONT(mime_get_mime_by_string("font/woff2")));
    ASSERT_TRUE(IS_MOBI(mime_get_mime_by_string("application/x-mobipocket-ebook")));
    ASSERT_TRUE(IS_RAW(mime_get_mime_by_string("image/x-nikon-nef")));

    ASSERT_FALSE(IS_PDF(mime_get_mime_by_string("text/plain")));
    ASSERT_FALSE(IS_ARC(mime_get_mime_by_string("text/plain")));
}

/**
 * Macro-enabled and template OOXML files are zip files: without an extension entry of their own,
 * they fall back to libmagic, which reports application/zip for the ones it cannot pick apart, and
 * the whole archive gets indexed member by member.
 */
TEST(Mime, MacroEnabledAndTemplateOoxmlAreDocuments) {
    const char *const EXTENSIONS[] = {
            "docx", "dotx", "docm", "dotm",
            "xlsx", "xltx", "xlsm", "xltm", "xlam", "xlsb",
            "pptx", "potx", "ppsx", "pptm", "potm", "ppsm", "ppam",
    };

    for (const char *ext: EXTENSIONS) {
        const unsigned int mime = mime_get_mime_by_ext(ext);

        ASSERT_NE(mime, 0u) << ext;
        ASSERT_TRUE(IS_DOC(mime)) << ext;
        ASSERT_FALSE(IS_ARC(mime)) << ext;
    }
}

/** Every mime id must map back to its own text, and no two ids may collide */
TEST(Mime, IdsAreUniqueAndReversible) {
    unsigned int *ids = get_mime_ids();

    ASSERT_NE(ids, nullptr);

    std::set<unsigned int> seen;

    for (int i = 0; ids[i] != 0; i++) {
        const char *text = mime_get_mime_text(ids[i]);

        ASSERT_NE(text, nullptr) << "mime id " << ids[i] << " has no text";
        ASSERT_EQ(mime_get_mime_by_string(text), ids[i]) << text;
        ASSERT_TRUE(seen.insert(ids[i]).second) << "duplicate mime id for " << text;
    }

    ASSERT_GT(seen.size(), 100);
}
