#include "tests/support/scan_fixture.h"

class EbookTest : public ScanTest {
protected:
    scan_ebook_ctx_t ctx = make_ebook_ctx();
};

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
