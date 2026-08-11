#include "tests/support/scan_fixture.h"

class MediaTest : public ScanTest {
protected:
    scan_media_ctx_t ctx = make_media_ctx();
};

/* Image */

TEST_F(MediaTest, ImageExifGps1) {
    load("media/exif_GPS.jpg");

    parse_media(&ctx, &f, &doc, "image/jpeg");

    ASSERT_STREQ(meta(MetaExifGpsLatitudeRef)->str_val, "N");
    ASSERT_STREQ(meta(MetaExifGpsLatitudeDMS)->str_val, "48:1 , 56585399:1000000, 0:1");

    ASSERT_STREQ(meta(MetaExifGpsLongitudeRef)->str_val, "E");
    ASSERT_STREQ(meta(MetaExifGpsLongitudeDMS)->str_val, "9:1 , 28046900:1000000, 0:1");
}

TEST_F(MediaTest, ImageExif1) {
    load("media/exiftest1.jpg");

    parse_media(&ctx, &f, &doc, "image/jpeg");

    ASSERT_STREQ(content(), "I don't know if it's a thing mostly done for high end "
                            "hotels or what, but I've seen it in a few places in Thailand: "
                            "There's a tradition of flower folding, doing a sort of light "
                            "origami with the petals of lotus and other flowers, to make "
                            "cute little ornaments.");
    ASSERT_STREQ(meta(MetaExifMake)->str_val, "NIKON CORPORATION");
    ASSERT_STREQ(meta(MetaExifModel)->str_val, "NIKON D7000");
    ASSERT_STREQ(meta(MetaExifDateTime)->str_val, "2019:11:08 14:37:59");
    ASSERT_STREQ(meta(MetaExifExposureTime)->str_val, "1:160");
    ASSERT_STREQ(meta(MetaArtist)->str_val, "FinalDoom");
    ASSERT_STREQ(meta(MetaExifSoftware)->str_val, "Adobe Photoshop Lightroom 5.7 (Windows)");
    ASSERT_STREQ(meta(MetaExifFNumber)->str_val, "53:10");
    ASSERT_STREQ(meta(MetaExifFocalLength)->str_val, "900:10");
    ASSERT_STREQ(meta(MetaExifIsoSpeedRatings)->str_val, "400");
    // The ASCII character code prefix is stripped, '@' is dropped by the text normalization
    ASSERT_STREQ(meta(MetaExifUserComment)->str_val, "If found:Nathaniel.Moseley gmail.com");
}

/** A thumbnail bigger than the source image is stored as-is instead of being scaled up */
TEST_F(MediaTest, ImageAsIsFs) {
    load("media/9555.jpg");

    parse_media(&ctx, &f, &doc, "image/jpeg");

    ASSERT_EQ(thumbnails_size(), 14098);
}

TEST_F(MediaTest, ImageDimensions) {
    load("media/9555.jpg");

    parse_media(&ctx, &f, &doc, "image/jpeg");

    ASSERT_NE(meta(MetaWidth), nullptr);
    ASSERT_NE(meta(MetaHeight), nullptr);
    ASSERT_GT(meta(MetaWidth)->long_val, 0);
    ASSERT_GT(meta(MetaHeight)->long_val, 0);
}

/* Video */

TEST_F(MediaTest, VidMkvSubDisabled) {
    load("media/berd.mkv");

    parse_media(&ctx, &f, &doc, "video/x-matroska");

    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_EQ(meta(MetaContent), nullptr);
}

TEST_F(MediaTest, VidMkvSubEnabled) {
    ctx.read_subtitles = TRUE;
    load("media/berd.mkv");

    parse_media(&ctx, &f, &doc, "video/x-matroska");

    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_NE(meta(MetaContent), nullptr);
}

TEST_F(MediaTest, Vid3Mp4) {
    load("media/vid3.mp4");

    parse_media(&ctx, &f, &doc, "video/mp4");

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Helicopter (((Accident))) - "
                                           "https://archive.org/details/Virginia_Helicopter_Crash");
    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "h264");
    ASSERT_EQ(meta(MetaMediaBitrate)->long_val, 826800);
    ASSERT_EQ(meta(MetaMediaDuration)->long_val, 10);
}

TEST_F(MediaTest, Vid3Ogv) {
    load("media/vid3.ogv");

    parse_media(&ctx, &f, &doc, "application/ogg");

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "theora");
    ASSERT_EQ(meta(MetaMediaBitrate)->long_val, 590261);
    ASSERT_EQ(meta(MetaMediaDuration)->long_val, 10);
}

TEST_F(MediaTest, Vid3Webm) {
    load("media/vid3.webm");

    parse_media(&ctx, &f, &doc, "video/webm");

    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "vp8");
    ASSERT_EQ(meta(MetaMediaBitrate)->long_val, 343153);
    ASSERT_EQ(meta(MetaMediaDuration)->long_val, 10);
}

/** Videos shorter than 15s get a single thumbnail, whatever tn_count says */
TEST_F(MediaTest, VidShortVideoIgnoresThumbnailCount) {
    ctx = make_media_ctx(500, 4);
    load("media/vid3.mp4");

    parse_media(&ctx, &f, &doc, "video/mp4");

    ASSERT_EQ(thumbnails_count(), 1);
    ASSERT_EQ(doc.thumbnail_count, 1);
}

TEST_F(MediaTest, VidThumbnailsDisabled) {
    ctx = make_media_ctx(500, 0);
    load("media/vid3.mp4");

    parse_media(&ctx, &f, &doc, "video/mp4");

    ASSERT_EQ(thumbnails_size(), 0);
    ASSERT_STREQ(meta(MetaMediaVideoCodec)->str_val, "h264");
}

TEST_F(MediaTest, VidDuplicateTags) {
    load("media/vid_tags.mkv");

    parse_media(&ctx, &f, &doc, "video/x-matroska");

    meta_line_t *meta_content = meta(MetaContent);
    ASSERT_STREQ(meta_content->str_val, "he's got a point");
    ASSERT_EQ(get_meta_from(meta_content->next, MetaContent), nullptr);

    meta_line_t *meta_title = meta(MetaTitle);
    ASSERT_STREQ(meta_title->str_val, "cool shit");
    ASSERT_EQ(get_meta_from(meta_title->next, MetaTitle), nullptr);

    meta_line_t *meta_artist = meta(MetaArtist);
    ASSERT_STREQ(meta_artist->str_val, "psychicpebbles");
    ASSERT_EQ(get_meta_from(meta_artist->next, MetaArtist), nullptr);
}

/* Audio */

//TODO: test music file with embedded cover art

TEST_F(MediaTest, MusicMp3) {
    load("media/02-The Watchmaker-Barry James_spoken.mp3");

    parse_media(&ctx, &f, &doc, "audio/x-mpeg-3");

    ASSERT_STREQ(meta(MetaArtist)->str_val, "Barry James");
    ASSERT_STREQ(meta(MetaAlbum)->str_val, "Strange Slumber, Music for Wonderful Dreams");
    ASSERT_STREQ(meta(MetaTitle)->str_val, "The Watchmaker");
    ASSERT_STREQ(meta(MetaGenre)->str_val, "New Age");
    ASSERT_STREQ(content(), "http://magnatune.com/artists/barry_james");
    ASSERT_STREQ(meta(MetaMediaAudioCodec)->str_val, "mp3");
}

/* Media read through an archive vfile rather than the filesystem */

class MediaArcTest : public ArcTest {
protected:
    scan_media_ctx_t media_ctx = make_media_ctx();
    scan_arc_ctx_t ctx = make_arc_ctx(ARC_MODE_RECURSE);

    void recurse_as(const char *mime) {
        recurse_into([this, mime](parse_job_t *job, document_t *sub_doc) {
            parse_media(&media_ctx, &job->vfile, sub_doc, mime);
        });
    }
};

TEST_F(MediaArcTest, JpegInTar) {
    recurse_as("image/jpeg");
    load("media/test.jpeg.tar");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_NE(thumbnail_size(sub_docs.last()), 0);
}

TEST_F(MediaArcTest, JpegInZipAsIs) {
    recurse_as("image/jpeg");
    load("media/test2.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_EQ(thumbnail_size(sub_docs.last()), 14098);
}

TEST_F(MediaArcTest, OgvInTar) {
    recurse_as("video/webm");
    load("arc/vid3.tar");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_EQ(get_meta(sub_docs.last(), MetaMediaBitrate)->long_val, 590261);
    ASSERT_EQ(get_meta(sub_docs.last(), MetaMediaDuration)->long_val, 10);
    ASSERT_NE(thumbnail_size(sub_docs.last()), 0);
}
