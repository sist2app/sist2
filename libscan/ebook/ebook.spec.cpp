#include "tests/support/scan_fixture.h"

#include <sstream>

extern "C" {
#include <mupdf/fitz.h>
}

class EbookTest : public ScanTest {
protected:
    scan_ebook_ctx_t ctx = make_ebook_ctx();
};

/**
 * Without colour management MuPDF approximates the colour spaces a PDF uses, and a magazine page
 * comes out as flat blocks of red and green (issue #93). The library has to be built with it.
 */
TEST(MupdfBuild, HasColourManagement) {
    fz_context *fzctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    ASSERT_NE(fzctx, nullptr);

    fz_enable_icc(fzctx);
    const int is_icc = fz_colorspace_is_icc(fzctx, fz_device_rgb(fzctx));

    fz_drop_context(fzctx);

    ASSERT_TRUE(is_icc);
}

TEST_F(EbookTest, CandlePdf) {
    load("ebook/General_-_Candle_Making.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Microsoft Word - A531 Candlemaking-01.doc");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Dafydd Prichard");
    ASSERT_NEAR(content_len(), 500, 4);
    ASSERT_NE(content()[0], ' ');
    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_EQ(meta(MetaPages)->long_val, 16);
}

TEST_F(EbookTest, CandlePdfNoThumbnail) {
    ctx.enable_tn = FALSE;
    load("ebook/General_-_Candle_Making.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_EQ(thumbnails_size(), 0);
    ASSERT_NEAR(content_len(), 500, 4);
}

/**
 * A page with no text of its own is read with OCR. The words have to come back whole: MuPDF's OCR
 * device lays a right-to-left script out one character per line, which used to reach the index as
 * "w e l c o m e" (issue #537).
 */
TEST_F(EbookTest, ScannedPageIsReadAsWords) {
    load("ebook/scanned_text.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_NE(meta(MetaContent), nullptr);
    ASSERT_NE(strstr(content(), "welcome to the library"), nullptr) << content();
}

/**
 * The same page in Arabic. MuPDF's OCR device lays a right-to-left script out one character per
 * line, so the words used to reach the index as "w e l c o m e" (issue #537). Needs the Arabic
 * language data, which is not part of the corpus.
 */
TEST_F(EbookTest, ScannedRightToLeftPageIsReadAsWords) {
    if (!tesseract_has_language("ara")) {
        GTEST_SKIP() << "ara.traineddata is not installed";
    }

    ctx.tesseract_lang = "ara";
    load("ebook/scanned_arabic.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_NE(meta(MetaContent), nullptr);
    // "hello world", whole, and in the order it is read in
    ASSERT_NE(strstr(content(), "\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645"), nullptr) << content();
}

static std::vector<size_t> parse_page_breaks(const char *csv) {
    std::vector<size_t> offsets;
    std::stringstream stream(csv);
    std::string offset;

    while (std::getline(stream, offset, ',')) {
        offsets.push_back(std::stoul(offset));
    }

    return offsets;
}

/** Codepoints, not bytes: the frontend reads the text back as UTF-16 */
static size_t codepoint_len(const char *str) {
    size_t count = 0;

    for (const char *c = str; *c != '\0'; c++) {
        if (((unsigned char) *c & 0xC0) != 0x80) {
            count += 1;
        }
    }

    return count;
}

/**
 * Where each page starts in the text, so that a search hit can be traced back to the page it is
 * on.
 */
TEST_F(EbookTest, PdfPageBreaks) {
    ctx = make_ebook_ctx(999999999999);
    ctx.tesseract_lang = nullptr;
    load("ebook/General_-_Candle_Making.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_NE(meta(MetaPageBreaks), nullptr);

    const std::vector<size_t> offsets = parse_page_breaks(meta(MetaPageBreaks)->str_val);

    ASSERT_EQ(offsets.size(), 16);
    ASSERT_EQ(offsets.front(), 0);
    ASSERT_LE(offsets.back(), codepoint_len(content()));

    for (size_t i = 1; i < offsets.size(); i++) {
        // A page with no text of its own starts where the one before it ended
        ASSERT_LE(offsets[i - 1], offsets[i]);
    }
}

/** The text of a page that was never read has no page to point at */
TEST_F(EbookTest, PdfPageBreaksStopWithTheContent) {
    load("ebook/General_-_Candle_Making.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_NE(meta(MetaPageBreaks), nullptr);
    ASSERT_LT(parse_page_breaks(meta(MetaPageBreaks)->str_val).size(), 16);
}

/** An offset that counted bytes would run past the end of a text that is not ASCII */
TEST_F(EbookTest, PdfPageBreaksCountCodepoints) {
    ctx = make_ebook_ctx(999999999999);
    ctx.tesseract_lang = nullptr;
    load("ebook/pdf2.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_NE(meta(MetaPageBreaks), nullptr);

    const std::vector<size_t> offsets = parse_page_breaks(meta(MetaPageBreaks)->str_val);

    ASSERT_LT(codepoint_len(content()), content_len());
    ASSERT_LE(offsets.back(), codepoint_len(content()));
}

TEST_F(EbookTest, Utf8Pdf) {
    load("ebook/utf8.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    ASSERT_TRUE(STR_STARTS_WITH_CONSTANT(content(), "最後測試 "));
}

TEST_F(EbookTest, Utf8PdfInvalidChars) {
    ctx = make_ebook_ctx(999999999999);
    ctx.tesseract_lang = nullptr;

    load("ebook/invalid_chars.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);

    // It should say "HART is a group of highly qualified ..." but the PDF
    //  text is been intentionally fucked with by the authors
    // We can at least filter out the non-printable/invalid characters like '�' etc
    ASSERT_TRUE(STR_STARTS_WITH_CONSTANT(content(), "HART i a g f highl alified "));
}

TEST_F(EbookTest, Pdf2) {
    load("ebook/pdf2.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);
}

TEST_F(EbookTest, PdfBlank) {
    load("ebook/blank.pdf");

    parse_ebook(&ctx, &f, "application/pdf", &doc);
}

TEST_F(EbookTest, Epub1) {
    load("ebook/epub1.epub");

    parse_ebook(&ctx, &f, "application/epub+zip", &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Rabies");
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(EbookTest, EpubFastMupdfError) {
    ctx = make_ebook_ctx(500, TRUE);
    load("ebook/mupdf-issue-129.epub");

    parse_ebook(&ctx, &f, "application/epub+zip", &doc);

    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(EbookTest, Epub1Fast) {
    ctx = make_ebook_ctx(500, TRUE);
    load("ebook/epub1.epub");

    parse_ebook(&ctx, &f, "application/epub+zip", &doc);

    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(EbookTest, EpubBlankFirstPage) {
    load("ebook/EpubBlankFirstPage.epub");

    parse_ebook(&ctx, &f, "application/epub+zip", &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Design Culture");
    ASSERT_NEAR(content_len(), 500, 4);
}
