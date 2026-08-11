#include "tests/support/scan_fixture.h"

class RawTest : public ScanTest {
protected:
    scan_raw_ctx_t ctx = make_raw_ctx();
};

/** Used to segfault while reading the thumbnail of this file */
TEST_F(RawTest, Segfault1) {
    load("raw/segfault1.dng");

    parse_raw(&ctx, &f, &doc);

    ASSERT_EQ(meta(MetaWidth)->long_val, 3840);
    ASSERT_EQ(meta(MetaHeight)->long_val, 7680);
}

TEST_F(RawTest, Panasonic) {
    load("raw/Panasonic.RW2");

    parse_raw(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "raw");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "DMC-GX8");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "Panasonic");
    ASSERT_STREQ(meta(MetaExifIsoSpeedRatings)->str_val, "640");
    ASSERT_STREQ(meta(MetaExifDateTime)->str_val, "2020:07:20 10:00:34");
    ASSERT_STREQ(meta(MetaExifFocalLength)->str_val, "20.0");
    ASSERT_STREQ(meta(MetaExifFNumber)->str_val, "2.0");
    ASSERT_EQ(meta(MetaWidth)->long_val, 5200);
    ASSERT_EQ(meta(MetaHeight)->long_val, 3904);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(RawTest, ExifGps1) {
    load("raw/exif_gps.DNG");

    parse_raw(&ctx, &f, &doc);

    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_STREQ(meta(MetaExifGpsLatitudeDec)->str_val, "48.943088531494141");
    ASSERT_STREQ(meta(MetaExifGpsLongitudeDec)->str_val, "9.467448234558105");
}

TEST_F(RawTest, Nikon) {
    load("raw/Nikon.NEF");

    parse_raw(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "raw");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "D750");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "Nikon");
    ASSERT_EQ(meta(MetaWidth)->long_val, 6032);
    ASSERT_EQ(meta(MetaHeight)->long_val, 4032);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(RawTest, Sony) {
    load("raw/Sony.ARW");

    parse_raw(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "raw");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "ILCE-7RM3");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "Sony");
    ASSERT_EQ(meta(MetaWidth)->long_val, 7968);
    ASSERT_EQ(meta(MetaHeight)->long_val, 5320);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(RawTest, Olympus) {
    load("raw/Olympus.ORF");

    parse_raw(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "raw");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "E-M5MarkII");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "Olympus");
    ASSERT_EQ(meta(MetaWidth)->long_val, 4640);
    ASSERT_EQ(meta(MetaHeight)->long_val, 3472);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(RawTest, Fuji) {
    load("raw/Fuji.RAF");

    parse_raw(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "raw");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "X-T2");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "Fujifilm");
    ASSERT_EQ(meta(MetaWidth)->long_val, 6032);
    ASSERT_EQ(meta(MetaHeight)->long_val, 4028);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(RawTest, ThumbnailDisabled) {
    ctx = make_raw_ctx(500, FALSE);
    load("raw/Nikon.NEF");

    parse_raw(&ctx, &f, &doc);

    ASSERT_EQ(thumbnails_size(), 0);
    ASSERT_EQ(meta(MetaWidth)->long_val, 6032);
}
