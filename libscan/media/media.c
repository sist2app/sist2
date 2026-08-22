#include "media.h"
#include "../ocr/ocr.h"
#include <ctype.h>
#include <libavutil/pixdesc.h>

#define MIN_SIZE 32
#define MAX_TILE_GRID_PIXELS (64 * 1024 * 1024)
#define AVIO_BUF_SIZE 8192
#define IS_VIDEO(fmt) ( \
    (fmt)->iformat->name && strcmp((fmt)->iformat->name, "image2") != 0 \
    && strcmp((fmt)->iformat->name, "jpeg_pipe") != 0 \
    && strcmp((fmt)->iformat->name, "webp_pipe") != 0 \
    && strcmp((fmt)->iformat->name, "png_pipe") != 0 \
    )


#define STORE_AS_IS ((void*)-1)

// Pointer to document being processed
__thread document_t *thread_doc;

const char *get_filepath_with_ext(document_t *doc, const char *filepath, const char *mime_str) {

    int has_extension = doc->ext > doc->base;

    if (!has_extension) {
        if (strcmp(mime_str, "image/png") == 0) {
            return "file.png";
        } else if (strcmp(mime_str, "image/jpeg") == 0) {
            return "file.jpg";
        }
    }

    return filepath;
}


__always_inline
void *scale_frame(const AVFrame *frame, enum AVCodecID codec_id, int size) {

    if (frame->pict_type == AV_PICTURE_TYPE_NONE) {
        return NULL;
    }

    int dstW;
    int dstH;
    if (frame->width <= size && frame->height <= size) {
        if (codec_id == AV_CODEC_ID_MJPEG || codec_id == AV_CODEC_ID_PNG) {
            return STORE_AS_IS;
        }

        dstW = frame->width;
        dstH = frame->height;
    } else {
        double ratio = (double) frame->width / frame->height;
        if (frame->width > frame->height) {
            dstW = size;
            dstH = (int) (size / ratio);
        } else {
            dstW = (int) (size * ratio);
            dstH = size;
        }
    }

    if (dstW <= MIN_SIZE || dstH <= MIN_SIZE) {
        return NULL;
    }

    AVFrame *scaled_frame = av_frame_alloc();

    struct SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height, frame->format,
            dstW, dstH, AV_PIX_FMT_YUV420P,
            SIST_SWS_ALGO, 0, 0, 0
    );

    int dst_buf_len = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, dstW, dstH, 1);
    uint8_t *dst_buf = (uint8_t *) av_malloc(dst_buf_len * 2);

    av_image_fill_arrays(scaled_frame->data, scaled_frame->linesize, dst_buf, AV_PIX_FMT_YUV420P, dstW, dstH, 1);

    sws_scale(sws_ctx,
              (const uint8_t *const *) frame->data, frame->linesize,
              0, frame->height,
              scaled_frame->data, scaled_frame->linesize
    );

    scaled_frame->width = dstW;
    scaled_frame->height = dstH;
    scaled_frame->format = AV_PIX_FMT_YUV420P;

    sws_freeContext(sws_ctx);

    return scaled_frame;
}

typedef struct {
    AVPacket *packet;
    AVFrame *frame;
} frame_and_packet_t;

static void frame_and_packet_free(frame_and_packet_t *frame_and_packet) {
    if (frame_and_packet->packet != NULL) {
        av_packet_free(&frame_and_packet->packet);
    }

    if (frame_and_packet->frame != NULL) {
        av_frame_free(&frame_and_packet->frame);
    }

    free(frame_and_packet->packet);
    free(frame_and_packet);
}

__always_inline
static void read_subtitles(UNUSED(scan_media_ctx_t *ctx), AVFormatContext *pFormatCtx, int stream_idx, document_t *doc) {

    text_buffer_t tex = text_buffer_create(-1);

    AVPacket packet;
    AVSubtitle subtitle;

    const AVCodec *subtitle_codec = avcodec_find_decoder(pFormatCtx->streams[stream_idx]->codecpar->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(subtitle_codec);
    decoder->thread_count = 1;
    avcodec_parameters_to_context(decoder, pFormatCtx->streams[stream_idx]->codecpar);
    avcodec_open2(decoder, subtitle_codec, NULL);

    int got_sub;

    while (1) {
        int read_frame_ret = av_read_frame(pFormatCtx, &packet);

        if (read_frame_ret != 0) {
            break;
        }

        if (packet.stream_index != stream_idx) {
            av_packet_unref(&packet);
            continue;
        }

        avcodec_decode_subtitle2(decoder, &subtitle, &got_sub, &packet);

        if (got_sub) {
            for (unsigned int i = 0; i < subtitle.num_rects; i++) {
                const char *text = subtitle.rects[i]->ass;

                if (text == NULL) {
                    continue;
                }

                char *idx = strstr(text, "\\N");
                if (idx != NULL && strlen(idx + 2) > 1) {
                    text_buffer_append_string0(&tex, idx + 2);
                    text_buffer_append_char(&tex, ' ');
                }
            }
            avsubtitle_free(&subtitle);
        }

        av_packet_unref(&packet);
    }

    text_buffer_terminate_string(&tex);

    APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);
    text_buffer_destroy(&tex);
    avcodec_free_context(&decoder);
}

__always_inline
static frame_and_packet_t *
read_frame(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *decoder, int stream_idx,
           document_t *doc) {

    frame_and_packet_t *result = calloc(1, sizeof(frame_and_packet_t));
    result->packet = av_packet_alloc();
    result->frame = av_frame_alloc();

    int receive_ret = -EAGAIN;
    while (receive_ret == -EAGAIN) {
        // Get video frame
        while (1) {
            int read_frame_ret = av_read_frame(pFormatCtx, result->packet);

            if (read_frame_ret != 0) {
                if (read_frame_ret != AVERROR_EOF) {
                    CTX_LOG_WARNINGF(doc->filepath,
                                     "(media.c) avcodec_read_frame() returned error code [%d] %s",
                                     read_frame_ret, av_err2str(read_frame_ret)
                    );
                }
                frame_and_packet_free(result);
                return NULL;
            }

            //Ignore audio/other frames
            if (result->packet->stream_index != stream_idx) {
                av_packet_unref(result->packet);
                continue;
            }
            break;
        }

        // Feed it to decoder
        int decode_ret = avcodec_send_packet(decoder, result->packet);
        if (decode_ret != 0) {
            CTX_LOG_ERRORF(doc->filepath,
                           "(media.c) avcodec_send_packet() returned error code [%d] %s",
                           decode_ret, av_err2str(decode_ret)
            );
            frame_and_packet_free(result);
            return NULL;
        }

        receive_ret = avcodec_receive_frame(decoder, result->frame);
        if (receive_ret == -EAGAIN && result->packet != NULL) {
            av_packet_unref(result->packet);
        }
    }

    return result;
}

void append_tag_meta_if_not_exists(scan_media_ctx_t *ctx, document_t *doc, AVDictionaryEntry *tag, enum metakey key) {

    if (meta_contains_key(doc->meta_head, key)) {
        CTX_LOG_DEBUGF(doc->filepath, "Ignoring duplicate tag: '%02x=%s'",
                       key, tag->value);
        return;
    }

    text_buffer_t tex = text_buffer_create(-1);
    text_buffer_append_string0(&tex, tag->value);
    text_buffer_terminate_string(&tex);
    meta_line_t *meta_tag = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur);
    meta_tag->key = key;
    strcpy(meta_tag->str_val, tex.dyn_buffer.buf);

    APPEND_META(doc, meta_tag);
    text_buffer_destroy(&tex);
}

#define APPEND_TAG_META(keyname) \
    APPEND_UTF8_META(doc, keyname, tag->value)

#define STRCPY_TOLOWER(dst, str) \
    strncpy(dst, str, sizeof(dst) - 1); \
    (dst)[sizeof(dst) - 1] = '\0'; \
    char *ptr = dst; \
    for (; *ptr; ++ptr) *ptr = (char) tolower(*ptr)

__always_inline
static void append_audio_meta(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, document_t *doc) {

    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(pFormatCtx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        char key[256];
        STRCPY_TOLOWER(key, tag->key);

        if (strcmp(key, "artist") == 0) {
            APPEND_TAG_META(MetaArtist);
        } else if (strcmp(key, "genre") == 0) {
            APPEND_TAG_META(MetaGenre);
        } else if (strcmp(key, "title") == 0) {
            APPEND_TAG_META(MetaTitle);
        } else if (strcmp(key, "album_artist") == 0) {
            APPEND_TAG_META(MetaAlbumArtist);
        } else if (strcmp(key, "album") == 0) {
            APPEND_TAG_META(MetaAlbum);
        } else if (strcmp(key, "comment") == 0) {
            append_tag_meta_if_not_exists(ctx, doc, tag, MetaContent);
            APPEND_TAG_META(MetaMediaComment);
        }
    }
}

/**
 * ffmpeg qualifies the tags of the EXIF and GPS sub-IFDs with the IFD name ("ExifIFD/FNumber") and pads
 * numeric values to fixed column widths ("      1:160    "); undo both so that the tag names and values
 * are the same regardless of which IFD a tag comes from.
 */
static AVDictionary *exif_metadata(const AVDictionary *frame_metadata) {

    AVDictionary *metadata = NULL;
    const AVDictionaryEntry *tag = NULL;

    while ((tag = av_dict_iterate(frame_metadata, tag))) {
        const char *name = strrchr(tag->key, '/');
        name = (name == NULL) ? tag->key : name + 1;

        char value[4096];
        char *ptr = value;
        for (const char *c = tag->value; *c != '\0' && ptr < value + sizeof(value) - 1; c++) {
            if (isspace((unsigned char) *c)) {
                if (ptr != value && *(ptr - 1) != ' ') {
                    *ptr++ = ' ';
                }
            } else {
                *ptr++ = *c;
            }
        }
        while (ptr != value && *(ptr - 1) == ' ') {
            ptr -= 1;
        }
        *ptr = '\0';

        av_dict_set(&metadata, name, value, AV_DICT_DONT_OVERWRITE);
    }

    return metadata;
}

/**
 * The EXIF UserComment tag has an undefined type, so ffmpeg renders it as a list of decimal bytes. Its
 * first 8 bytes are a character code ("ASCII\0\0\0", "UNICODE\0", "JIS\0\0\0\0\0", or all-zero when the
 * encoding is undefined); decode the ASCII flavor (and the undefined one, which is ASCII in practice).
 */
static int exif_decode_user_comment(const char *value, char *buf, size_t size) {

    static const char ascii_code[8] = {'A', 'S', 'C', 'I', 'I', 0, 0, 0};
    static const char undefined_code[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    char bytes[4096];
    size_t count = 0;

    for (const char *ptr = value; *ptr != '\0'; ptr += strspn(ptr, ", ")) {
        char *end;
        long byte = strtol(ptr, &end, 10);

        if (end == ptr || byte < 0 || byte > UINT8_MAX || count == sizeof(bytes)) {
            return FALSE;
        }

        bytes[count++] = (char) byte;
        ptr = end;
    }

    if (count <= sizeof(ascii_code)
        || (memcmp(bytes, ascii_code, sizeof(ascii_code)) != 0
            && memcmp(bytes, undefined_code, sizeof(undefined_code)) != 0)) {
        return FALSE;
    }

    const char *text = bytes + sizeof(ascii_code);
    size_t len = count - sizeof(ascii_code);

    while (len > 0 && (text[len - 1] == '\0' || text[len - 1] == ' ')) {
        len -= 1;
    }

    if (len == 0 || len >= size) {
        return FALSE;
    }

    memcpy(buf, text, len);
    buf[len] = '\0';

    return TRUE;
}

__always_inline
static void
append_video_meta(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, AVFrame *frame, document_t *doc, int is_video) {

    if (is_video) {
        if (pFormatCtx->duration / AV_TIME_BASE != 0) {
            meta_line_t *meta_duration = malloc(sizeof(meta_line_t));
            meta_duration->key = MetaMediaDuration;
            meta_duration->long_val = pFormatCtx->duration / AV_TIME_BASE;
            if (meta_duration->long_val > INT32_MAX) {
                meta_duration->long_val = 0;
            }
            APPEND_META(doc, meta_duration);
        }

        if (pFormatCtx->bit_rate != 0) {
            meta_line_t *meta_bitrate = malloc(sizeof(meta_line_t));
            meta_bitrate->key = MetaMediaBitrate;
            meta_bitrate->long_val = pFormatCtx->bit_rate;
            APPEND_META(doc, meta_bitrate);
        }
    }

    AVDictionaryEntry *tag = NULL;
    if (is_video) {
        while ((tag = av_dict_get(pFormatCtx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            char key[256];
            STRCPY_TOLOWER(key, tag->key);

            if (strcmp(key, "title") == 0) {
                append_tag_meta_if_not_exists(ctx, doc, tag, MetaTitle);
            } else if (strcmp(key, "comment") == 0) {
                append_tag_meta_if_not_exists(ctx, doc, tag, MetaContent);
            } else if (strcmp(key, "artist") == 0) {
                append_tag_meta_if_not_exists(ctx, doc, tag, MetaArtist);
            }
        }
    } else {
        // EXIF metadata
        AVDictionary *metadata = exif_metadata(frame->metadata);

        while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            char key[256];
            STRCPY_TOLOWER(key, tag->key);

            if (strcmp(key, "artist") == 0) {
                append_tag_meta_if_not_exists(ctx, doc, tag, MetaArtist);
            } else if (strcmp(key, "imagedescription") == 0) {
                append_tag_meta_if_not_exists(ctx, doc, tag, MetaContent);
            } else if (strcmp(key, "make") == 0) {
                APPEND_TAG_META(MetaExifMake);
            } else if (strcmp(key, "model") == 0) {
                APPEND_TAG_META(MetaExifModel);
            } else if (strcmp(key, "software") == 0) {
                APPEND_TAG_META(MetaExifSoftware);
            } else if (strcmp(key, "fnumber") == 0) {
                APPEND_TAG_META(MetaExifFNumber);
            } else if (strcmp(key, "focallength") == 0) {
                APPEND_TAG_META(MetaExifFocalLength);
            } else if (strcmp(key, "usercomment") == 0) {
                char comment[4096];

                if (exif_decode_user_comment(tag->value, comment, sizeof(comment))) {
                    APPEND_UTF8_META(doc, MetaExifUserComment, comment);
                } else {
                    APPEND_TAG_META(MetaExifUserComment);
                }
            } else if (strcmp(key, "isospeedratings") == 0) {
                APPEND_TAG_META(MetaExifIsoSpeedRatings);
            } else if (strcmp(key, "exposuretime") == 0) {
                APPEND_TAG_META(MetaExifExposureTime);
            } else if (strcmp(key, "datetime") == 0) {
                APPEND_TAG_META(MetaExifDateTime);
            } else if (strcmp(key, "gpslatitude") == 0) {
                APPEND_TAG_META(MetaExifGpsLatitudeDMS);
            } else if (strcmp(key, "gpslatituderef") == 0) {
                APPEND_TAG_META(MetaExifGpsLatitudeRef);
            } else if (strcmp(key, "gpslongitude") == 0) {
                APPEND_TAG_META(MetaExifGpsLongitudeDMS);
            } else if (strcmp(key, "gpslongituderef") == 0) {
                APPEND_TAG_META(MetaExifGpsLongitudeRef);
            }
        }

        av_dict_free(&metadata);
    }
}

static void ocr_image_cb(const char *text, UNUSED(size_t len)) {
    APPEND_STR_META(thread_doc, MetaContent, text);
}

#define OCR_PIXEL_FORMAT AV_PIX_FMT_RGB32
#define OCR_BYTES_PER_PIXEL 4
#define OCR_PIXELS_PER_INCH 70

void ocr_image(scan_media_ctx_t *ctx, document_t *doc, AVFrame *frame) {

    // Convert to RGB32
    AVFrame *rgb_frame = av_frame_alloc();

    struct SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height, frame->format,
            frame->width, frame->height, OCR_PIXEL_FORMAT,
            SWS_LANCZOS, 0, 0, 0
    );

    int dst_buf_len = av_image_get_buffer_size(OCR_PIXEL_FORMAT, frame->width, frame->height, 1);
    uint8_t *dst_buf = (uint8_t *) av_malloc(dst_buf_len * 2);

    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, dst_buf, OCR_PIXEL_FORMAT, frame->width, frame->height,
                         1);

    sws_scale(sws_ctx,
              (const uint8_t *const *) frame->data, frame->linesize,
              0, frame->height,
              rgb_frame->data, rgb_frame->linesize
    );

    thread_doc = doc;
    ocr_extract_text(
            ctx->tesseract_path,
            ctx->tesseract_lang,
            rgb_frame->data[0],
            frame->width,
            frame->height,
            OCR_BYTES_PER_PIXEL,
            rgb_frame->linesize[0],
            OCR_PIXELS_PER_INCH,
            ocr_image_cb
    );

    sws_freeContext(sws_ctx);
    av_free(*rgb_frame->data);
    av_frame_free(&rgb_frame);
}

#define SAVE_THUMBNAIL_OK 0
#define SAVE_THUMBNAIL_SKIPPED 1
#define SAVE_THUMBNAIL_FAILED 2

int decode_frame_and_save_thumbnail(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *decoder,
                                    AVStream *stream, int video_stream, document_t *doc, double seek_ratio,
                                    int thumbnail_index) {

    if (IS_VIDEO(pFormatCtx) && stream->codecpar->codec_id != AV_CODEC_ID_GIF) {
        int seek_ok = FALSE;

        double target_timestamp = (double) pFormatCtx->duration * seek_ratio;
        long ts = (long) target_timestamp;

        int seek_ret = avformat_seek_file(
                // Allow +- 1s
                pFormatCtx, -1, ts - AV_TIME_BASE, ts, ts + AV_TIME_BASE,
                0
        );

        if (seek_ret == 0) {
            seek_ok = TRUE;
        } else {
            CTX_LOG_DEBUGF(
                    doc->filepath,
                    "(media.c) Could not seek media file: %s", av_err2str(seek_ret)
            );
        }

        if (seek_ok == FALSE && thumbnail_index != 0) {
            CTX_LOG_WARNING(doc->filepath,
                            "(media.c) Could not seek media file. Can't generate additional thumbnails.");
            return SAVE_THUMBNAIL_FAILED;
        }
    }

    frame_and_packet_t *frame_and_packet = read_frame(ctx, pFormatCtx, decoder, video_stream, doc);
    if (frame_and_packet == NULL) {
        return SAVE_THUMBNAIL_FAILED;
    }

    if (ctx->tesseract_lang != NULL && thumbnail_index == 0 && !meta_contains_key(doc->meta_head, MetaContent)) {
        ocr_image(ctx, doc, frame_and_packet->frame);
    }

    // NOTE: OCR'd content takes precedence over exif image description
    if (thumbnail_index == 0) {
        append_video_meta(ctx, pFormatCtx, frame_and_packet->frame, doc, IS_VIDEO(pFormatCtx));
    }

    // Scale frame
    AVFrame *scaled_frame = scale_frame(frame_and_packet->frame, decoder->codec_id, ctx->tn_size);

    if (scaled_frame == NULL) {
        frame_and_packet_free(frame_and_packet);
        return SAVE_THUMBNAIL_FAILED;
    }

    int return_value;

    if (scaled_frame == STORE_AS_IS) {
        return_value = SAVE_THUMBNAIL_OK;

        APPEND_THUMBNAIL(doc, frame_and_packet->packet->data, frame_and_packet->packet->size);
    } else {
        // Encode frame
        AVCodecContext *thumbnail_encoder = alloc_webp_encoder(scaled_frame->width, scaled_frame->height,
                                                               ctx->tn_qscale);
        avcodec_send_frame(thumbnail_encoder, scaled_frame);
        avcodec_send_frame(thumbnail_encoder, NULL); // send EOF

        AVPacket *thumbnail_packet = av_packet_alloc();
        avcodec_receive_packet(thumbnail_encoder, thumbnail_packet);

        // Save thumbnail
        if (thumbnail_index == 0) {
            APPEND_THUMBNAIL(doc, thumbnail_packet->data, thumbnail_packet->size);
            return_value = SAVE_THUMBNAIL_OK;

        } else if (thumbnail_index > 1) {
            // TO FIX: the 2nd rendered frame is always broken, just skip it until
            //  I figure out a better fix.
            thumbnail_index -= 1;

            APPEND_THUMBNAIL(doc, thumbnail_packet->data, thumbnail_packet->size);

            return_value = SAVE_THUMBNAIL_OK;
        } else {
            return_value = SAVE_THUMBNAIL_SKIPPED;
        }

        avcodec_free_context(&thumbnail_encoder);
        av_packet_free(&thumbnail_packet);
        av_free(*scaled_frame->data);
        av_frame_free(&scaled_frame);
    }

    frame_and_packet_free(frame_and_packet);
    return return_value;
}

/**
 * A picture can carry companion images next to the one it is of - an HDR gain map, a depth map -
 * and those are greyscale, so they are only worth looking at when there is nothing else.
 */
static int is_auxiliary_video_stream(const AVStream *stream, int has_color_video_stream) {

    if (stream->disposition & AV_DISPOSITION_DEPENDENT) {
        return TRUE;
    }

    if (!has_color_video_stream) {
        return FALSE;
    }

    const AVPixFmtDescriptor *pix_fmt = av_pix_fmt_desc_get(stream->codecpar->format);

    return pix_fmt != NULL && pix_fmt->nb_components == 1;
}

static int format_has_color_video_stream(const AVFormatContext *pFormatCtx) {

    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        const AVStream *stream = pFormatCtx->streams[i];

        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO
            || (stream->disposition & AV_DISPOSITION_DEPENDENT)) {
            continue;
        }

        const AVPixFmtDescriptor *pix_fmt = av_pix_fmt_desc_get(stream->codecpar->format);
        if (pix_fmt != NULL && pix_fmt->nb_components > 1) {
            return TRUE;
        }
    }

    return FALSE;
}

static void append_encoded_thumbnail(scan_media_ctx_t *ctx, document_t *doc, AVFrame *scaled_frame) {

    AVCodecContext *thumbnail_encoder = alloc_webp_encoder(scaled_frame->width, scaled_frame->height, ctx->tn_qscale);

    avcodec_send_frame(thumbnail_encoder, scaled_frame);
    avcodec_send_frame(thumbnail_encoder, NULL); // send EOF

    AVPacket *thumbnail_packet = av_packet_alloc();
    avcodec_receive_packet(thumbnail_encoder, thumbnail_packet);

    doc->thumbnail_count = 1;
    APPEND_THUMBNAIL(doc, thumbnail_packet->data, thumbnail_packet->size);

    av_packet_free(&thumbnail_packet);
    avcodec_free_context(&thumbnail_encoder);
}

static const AVStreamGroup *find_tile_grid(const AVFormatContext *pFormatCtx) {

    for (unsigned int i = 0; i < pFormatCtx->nb_stream_groups; i++) {
        const AVStreamGroup *group = pFormatCtx->stream_groups[i];

        if (group->type == AV_STREAM_GROUP_PARAMS_TILE_GRID && group->nb_streams > 0
            && group->params.tile_grid->nb_tiles > 0) {
            return group;
        }
    }

    return NULL;
}

/**
 * Recent phones store a HEIF picture as a grid of separately coded tiles; the full image exists
 * only as the stream group that says where each tile goes, so it has to be assembled here.
 */
static AVFrame *decode_tile_grid(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx,
                                 const AVStreamGroup *group, document_t *doc) {

    const AVStreamGroupTileGrid *grid = group->params.tile_grid;

    if ((int64_t) grid->coded_width * grid->coded_height > MAX_TILE_GRID_PIXELS) {
        CTX_LOG_WARNINGF(doc->filepath, "(media.c) Tile grid is too large to assemble: %dx%d",
                         grid->coded_width, grid->coded_height);
        return NULL;
    }

    int *tile_of_stream = malloc(sizeof(int) * pFormatCtx->nb_streams);
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        tile_of_stream[i] = -1;
    }
    for (unsigned int i = 0; i < group->nb_streams && i < grid->nb_tiles; i++) {
        tile_of_stream[group->streams[i]->index] = (int) i;
    }

    const AVCodecParameters *codecpar = group->streams[0]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    decoder->thread_count = 1;
    avcodec_parameters_to_context(decoder, codecpar);
    avcodec_open2(decoder, codec, NULL);

    AVFrame *canvas = av_frame_alloc();
    canvas->format = AV_PIX_FMT_YUV420P;
    canvas->width = grid->coded_width;
    canvas->height = grid->coded_height;

    if (av_frame_get_buffer(canvas, 0) != 0) {
        CTX_LOG_ERRORF(doc->filepath, "(media.c) Could not allocate a %dx%d canvas for the tile grid",
                       grid->coded_width, grid->coded_height);
        av_frame_free(&canvas);
        avcodec_free_context(&decoder);
        free(tile_of_stream);
        return NULL;
    }

    const ptrdiff_t canvas_linesize[4] = {
            canvas->linesize[0], canvas->linesize[1], canvas->linesize[2], canvas->linesize[3]
    };
    av_image_fill_black(canvas->data, canvas_linesize, canvas->format, AVCOL_RANGE_MPEG,
                        canvas->width, canvas->height);

    AVFrame *tile = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    struct SwsContext *sws_ctx = NULL;
    unsigned int tiles_decoded = 0;

    while (tiles_decoded < grid->nb_tiles && av_read_frame(pFormatCtx, packet) == 0) {

        int tile_index = tile_of_stream[packet->stream_index];
        if (tile_index == -1) {
            av_packet_unref(packet);
            continue;
        }

        // Every tile is a single coded picture, so the decoder only hands it over once it is drained
        avcodec_send_packet(decoder, packet);
        avcodec_send_packet(decoder, NULL);
        int receive_ret = avcodec_receive_frame(decoder, tile);
        avcodec_flush_buffers(decoder);
        av_packet_unref(packet);

        if (receive_ret != 0) {
            CTX_LOG_DEBUGF(doc->filepath, "(media.c) Could not decode tile %d: %s",
                           tile_index, av_err2str(receive_ret));
            continue;
        }

        int x = grid->offsets[tile_index].horizontal;
        int y = grid->offsets[tile_index].vertical;

        if (x < 0 || y < 0 || x + tile->width > canvas->width || y + tile->height > canvas->height) {
            CTX_LOG_DEBUGF(doc->filepath, "(media.c) Tile %d does not fit the canvas", tile_index);
            av_frame_unref(tile);
            continue;
        }

        sws_ctx = sws_getCachedContext(sws_ctx, tile->width, tile->height, tile->format,
                                       tile->width, tile->height, canvas->format,
                                       SWS_POINT, 0, 0, 0);

        uint8_t *tile_dst[4] = {
                canvas->data[0] + y * canvas->linesize[0] + x,
                canvas->data[1] + (y / 2) * canvas->linesize[1] + (x / 2),
                canvas->data[2] + (y / 2) * canvas->linesize[2] + (x / 2),
                NULL
        };

        sws_scale(sws_ctx, (const uint8_t *const *) tile->data, tile->linesize, 0, tile->height,
                  tile_dst, canvas->linesize);

        if (tiles_decoded == 0) {
            av_dict_copy(&canvas->metadata, tile->metadata, 0);
        }

        tiles_decoded += 1;
        av_frame_unref(tile);
    }

    sws_freeContext(sws_ctx);
    av_packet_free(&packet);
    av_frame_free(&tile);
    avcodec_free_context(&decoder);
    free(tile_of_stream);

    if (tiles_decoded == 0) {
        CTX_LOG_ERROR(doc->filepath, "(media.c) Could not decode any tile of the image");
        av_frame_free(&canvas);
        return NULL;
    }

    if (tiles_decoded != grid->nb_tiles) {
        CTX_LOG_WARNINGF(doc->filepath, "(media.c) Only %u of the %u tiles of the image could be decoded",
                         tiles_decoded, grid->nb_tiles);
    }

    // The grid is coded in whole tiles; the picture is the crop of it that the file asks for
    canvas->data[0] += grid->vertical_offset * canvas->linesize[0] + grid->horizontal_offset;
    canvas->data[1] += (grid->vertical_offset / 2) * canvas->linesize[1] + (grid->horizontal_offset / 2);
    canvas->data[2] += (grid->vertical_offset / 2) * canvas->linesize[2] + (grid->horizontal_offset / 2);
    canvas->width = grid->width;
    canvas->height = grid->height;
    canvas->pict_type = AV_PICTURE_TYPE_I;

    return canvas;
}

static void parse_tile_grid_image(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx,
                                  const AVStreamGroup *group, document_t *doc) {

    const AVStreamGroupTileGrid *grid = group->params.tile_grid;

    const AVCodecDescriptor *desc = avcodec_descriptor_get(group->streams[0]->codecpar->codec_id);
    if (desc != NULL) {
        APPEND_STR_META(doc, MetaMediaVideoCodec, desc->name);
    }

    meta_line_t *meta_w = malloc(sizeof(meta_line_t));
    meta_w->key = MetaWidth;
    meta_w->long_val = grid->width;
    APPEND_META(doc, meta_w);

    meta_line_t *meta_h = malloc(sizeof(meta_line_t));
    meta_h->key = MetaHeight;
    meta_h->long_val = grid->height;
    APPEND_META(doc, meta_h);

    if (ctx->tn_count == 0 || grid->width <= MIN_SIZE || grid->height <= MIN_SIZE) {
        return;
    }

    AVFrame *canvas = decode_tile_grid(ctx, pFormatCtx, group, doc);
    if (canvas == NULL) {
        return;
    }

    if (ctx->tesseract_lang != NULL && !meta_contains_key(doc->meta_head, MetaContent)) {
        ocr_image(ctx, doc, canvas);
    }

    append_video_meta(ctx, pFormatCtx, canvas, doc, FALSE);

    AVFrame *scaled_frame = scale_frame(canvas, group->streams[0]->codecpar->codec_id, ctx->tn_size);

    if (scaled_frame != NULL && scaled_frame != STORE_AS_IS) {
        append_encoded_thumbnail(ctx, doc, scaled_frame);

        av_free(*scaled_frame->data);
        av_frame_free(&scaled_frame);
    }

    av_frame_free(&canvas);
}

void parse_media_format_ctx(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, document_t *doc) {

    int video_stream = -1;
    int audio_stream = -1;
    int subtitle_stream = -1;

    avformat_find_stream_info(pFormatCtx, NULL);

    const AVStreamGroup *tile_grid = find_tile_grid(pFormatCtx);
    if (tile_grid != NULL) {
        parse_tile_grid_image(ctx, pFormatCtx, tile_grid, doc);

        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        return;
    }

    const int has_color_video_stream = format_has_color_video_stream(pFormatCtx);

    for (int i = (int) pFormatCtx->nb_streams - 1; i >= 0; i--) {
        AVStream *stream = pFormatCtx->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && is_auxiliary_video_stream(stream, has_color_video_stream)) {
            continue;
        }

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_stream == -1) {
                const AVCodecDescriptor *desc = avcodec_descriptor_get(stream->codecpar->codec_id);

                if (desc != NULL) {
                    APPEND_STR_META(doc, MetaMediaAudioCodec, desc->name);
                }

                audio_stream = i;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

            if (video_stream == -1) {
                const AVCodecDescriptor *desc = avcodec_descriptor_get(stream->codecpar->codec_id);

                if (desc != NULL) {
                    APPEND_STR_META(doc, MetaMediaVideoCodec, desc->name);
                }

                meta_line_t *meta_w = malloc(sizeof(meta_line_t));
                meta_w->key = MetaWidth;
                meta_w->long_val = stream->codecpar->width;
                APPEND_META(doc, meta_w);

                meta_line_t *meta_h = malloc(sizeof(meta_line_t));
                meta_h->key = MetaHeight;
                meta_h->long_val = stream->codecpar->height;
                APPEND_META(doc, meta_h);

                video_stream = i;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            subtitle_stream = i;
        }
    }

    if (subtitle_stream != -1 && ctx->read_subtitles) {
        read_subtitles(ctx, pFormatCtx, subtitle_stream, doc);

        // Reset stream
        if (video_stream != -1) {
            av_seek_frame(pFormatCtx, video_stream, 0, 0);
        }
    }

    if (audio_stream != -1) {
        append_audio_meta(ctx, pFormatCtx, doc);
    }

    if (video_stream != -1 && ctx->tn_count > 0) {
        AVStream *stream = pFormatCtx->streams[video_stream];

        if (stream->codecpar->width <= MIN_SIZE || stream->codecpar->height <= MIN_SIZE) {
            CTX_LOG_DEBUGF(doc->filepath,
                           "Will not generate thumbnail because image is too small: %dx%d",
                           stream->codecpar->width, stream->codecpar->width);
            avformat_close_input(&pFormatCtx);
            avformat_free_context(pFormatCtx);
            return;
        }

        // Decoder
        const AVCodec *video_codec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *decoder = avcodec_alloc_context3(video_codec);
        decoder->thread_count = 1;
        avcodec_parameters_to_context(decoder, stream->codecpar);
        avcodec_open2(decoder, video_codec, NULL);

        int video_duration_in_seconds = (int) (pFormatCtx->duration / AV_TIME_BASE);

        int thumbnails_to_generate = (IS_VIDEO(pFormatCtx) && stream->codecpar->codec_id != AV_CODEC_ID_GIF &&
                                      video_duration_in_seconds >= 15)
                                     // Limit to ~1 thumbnail every 7s
                                     ? MAX(MIN(ctx->tn_count, video_duration_in_seconds / 7 + 1), 1) + 1
                                     : 1;

        const double seek_increment = thumbnails_to_generate == 1
                                      ? 0.10
                                      : 1.0 / (thumbnails_to_generate + 1);

        int number_of_thumbnails_generated = 0;
        int save_thumbnail_ret;

        for (int i = 0; i < thumbnails_to_generate; i++) {
            double seek_ratio = seek_increment * i + seek_increment * 0.9;

            save_thumbnail_ret = decode_frame_and_save_thumbnail(ctx, pFormatCtx, decoder, stream, video_stream, doc,
                                                                 seek_ratio, i);
            if (save_thumbnail_ret == SAVE_THUMBNAIL_FAILED) {
                break;
            }

            if (save_thumbnail_ret == SAVE_THUMBNAIL_OK) {
                number_of_thumbnails_generated += 1;
            }
        }

        if (number_of_thumbnails_generated > 0) {
            doc->thumbnail_count = number_of_thumbnails_generated;
        }

        avcodec_free_context(&decoder);
    }

    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);
}

void parse_media_filename(scan_media_ctx_t *ctx, const char *filepath, document_t *doc) {

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if (pFormatCtx == NULL) {
        CTX_LOG_ERROR(doc->filepath, "(media.c) Could not allocate context with avformat_alloc_context()");
        return;
    }
    pFormatCtx->max_analyze_duration = 100000000;
    pFormatCtx->probesize = 100000000;

    int res = avformat_open_input(&pFormatCtx, filepath, NULL, NULL);
    if (res < 0) {
        CTX_LOG_ERRORF(doc->filepath, "(media.c) avformat_open_input() returned [%d] %s", res, av_err2str(res));
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        return;
    }

    parse_media_format_ctx(ctx, pFormatCtx, doc);
}

int vfile_read(void *ptr, uint8_t *buf, int buf_size) {
    struct vfile *f = ptr;

    int ret = f->read(f, buf, buf_size);

    if (ret == 0) {
        return AVERROR_EOF;
    }
    return ret;
}

typedef struct {
    size_t size;
    FILE *file;
    void *buf;
} memfile_t;

int memfile_read(void *ptr, uint8_t *buf, int buf_size) {
    memfile_t *mem = ptr;

    size_t ret = fread(buf, 1, buf_size, mem->file);

    if (ret == 0 && feof(mem->file)) {
        return AVERROR_EOF;
    }

    return (int) ret;
}

long memfile_seek(void *ptr, long offset, int whence) {
    memfile_t *mem = ptr;

    if (whence == 0x10000) {
        return (long) mem->size;
    }

    int ret = fseek(mem->file, offset, whence);
    if (ret != 0) {
        return AVERROR_EOF;
    }

    return ftell(mem->file);
}

int memfile_open(vfile_t *f, memfile_t *mem) {
    mem->size = f->st_size;

    mem->buf = malloc(mem->size);
    if (mem->buf == NULL) {
        return -1;
    }

    int ret = f->read(f, mem->buf, mem->size);
    mem->file = fmemopen(mem->buf, mem->size, "rb");

    if (f->calculate_checksum) {
        safe_digest_update(f->sha1_ctx, mem->buf, mem->size);
        EVP_DigestFinal_ex(f->sha1_ctx, f->sha1_digest, NULL);
        EVP_MD_CTX_free(f->sha1_ctx);
        f->sha1_ctx = NULL;
        f->has_checksum = TRUE;
    }

    return ((size_t) ret == mem->size && mem->file != NULL) ? 0 : -1;
}

int memfile_open_buf(void *buf, size_t buf_len, memfile_t *mem) {
    mem->size = (int) buf_len;

    mem->buf = buf;
    mem->file = fmemopen(mem->buf, mem->size, "rb");

    return mem->file != NULL ? 0 : -1;
}

void memfile_close(memfile_t *mem) {
    if (mem->buf != NULL) {
        free(mem->buf);
        fclose(mem->file);
    }
}

void parse_media_vfile(scan_media_ctx_t *ctx, struct vfile *f, document_t *doc, const char *mime_str) {

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if (pFormatCtx == NULL) {
        CTX_LOG_ERROR(doc->filepath, "(media.c) Could not allocate context with avformat_alloc_context()");
        return;
    }
    pFormatCtx->max_analyze_duration = 100000000;
    pFormatCtx->probesize = 100000000;


    unsigned char *buffer = (unsigned char *) av_malloc(AVIO_BUF_SIZE);
    AVIOContext *io_ctx = NULL;
    memfile_t memfile = {0, 0, 0};

    const char *filepath = get_filepath_with_ext(doc, f->filepath, mime_str);

    if (f->st_size <= (size_t) ctx->max_media_buffer) {
        int ret = memfile_open(f, &memfile);
        if (ret == 0) {
            CTX_LOG_DEBUGF(f->filepath, "Loading media file in memory (%ldB)", f->st_size);
            io_ctx = avio_alloc_context(buffer, AVIO_BUF_SIZE, 0, &memfile, memfile_read, NULL, memfile_seek);
        }
    }

    if (io_ctx == NULL) {
        CTX_LOG_DEBUG(f->filepath, "Reading media file without seek support");
        io_ctx = avio_alloc_context(buffer, AVIO_BUF_SIZE, 0, f, vfile_read, NULL, NULL);
    }

    pFormatCtx->pb = io_ctx;

    int res = avformat_open_input(&pFormatCtx, filepath, NULL, NULL);
    if (res < 0) {
        if (res != -5) {
            CTX_LOG_ERRORF(doc->filepath, "(media.c) avformat_open_input() returned [%d] %s", res, av_err2str(res));
        }
        av_free(io_ctx->buffer);
        memfile_close(&memfile);
        avio_context_free(&io_ctx);
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        return;
    }

    parse_media_format_ctx(ctx, pFormatCtx, doc);
    av_free(io_ctx->buffer);
    avio_context_free(&io_ctx);
    memfile_close(&memfile);
}

void parse_media(scan_media_ctx_t *ctx, vfile_t *f, document_t *doc, const char *mime_str) {

    if (f->is_fs_file) {
        parse_media_filename(ctx, f->filepath, doc);
    } else {
        parse_media_vfile(ctx, f, doc, mime_str);
    }
}

void init_media() {
    av_log_set_level(AV_LOG_QUIET);
}

int store_image_thumbnail(scan_media_ctx_t *ctx, void *buf, size_t buf_len, document_t *doc, const char *url) {
    memfile_t memfile = {0, 0, 0};
    AVIOContext *io_ctx = NULL;

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if (pFormatCtx == NULL) {
        CTX_LOG_ERROR(doc->filepath, "(media.c) Could not allocate context with avformat_alloc_context()");
        return FALSE;
    }
    pFormatCtx->max_analyze_duration = 100000000;
    pFormatCtx->probesize = 100000000;

    unsigned char *buffer = (unsigned char *) av_malloc(AVIO_BUF_SIZE);

    int ret = memfile_open_buf(buf, buf_len, &memfile);
    if (ret == 0) {
        CTX_LOG_DEBUGF(doc->filepath, "Loading media file in memory (%ldB)", buf_len);
        io_ctx = avio_alloc_context(buffer, AVIO_BUF_SIZE, 0, &memfile, memfile_read, NULL, memfile_seek);
    } else {
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        fclose(memfile.file);
        return FALSE;
    }

    pFormatCtx->pb = io_ctx;

    int res = avformat_open_input(&pFormatCtx, url, NULL, NULL);
    if (res != 0) {
        av_free(io_ctx->buffer);
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        avio_context_free(&io_ctx);
        fclose(memfile.file);
        return FALSE;
    }

    AVStream *stream = pFormatCtx->streams[0];

    // Decoder
    const AVCodec *video_codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(video_codec);
    decoder->thread_count = 1;
    avcodec_parameters_to_context(decoder, stream->codecpar);
    avcodec_open2(decoder, video_codec, NULL);

    frame_and_packet_t *frame_and_packet = read_frame(ctx, pFormatCtx, decoder, 0, doc);
    if (frame_and_packet == NULL) {
        avcodec_free_context(&decoder);
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        av_free(io_ctx->buffer);
        avio_context_free(&io_ctx);
        fclose(memfile.file);
        return FALSE;
    }

    // Scale frame
    AVFrame *scaled_frame = scale_frame(frame_and_packet->frame, decoder->codec_id, ctx->tn_size);

    if (scaled_frame == NULL) {
        frame_and_packet_free(frame_and_packet);
        avcodec_free_context(&decoder);
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
        av_free(io_ctx->buffer);
        avio_context_free(&io_ctx);
        fclose(memfile.file);
        return FALSE;
    }

    if (scaled_frame == STORE_AS_IS) {
        doc->thumbnail_count = 1;
        APPEND_THUMBNAIL(doc, frame_and_packet->packet->data, frame_and_packet->packet->size);
    } else {
        append_encoded_thumbnail(ctx, doc, scaled_frame);

        av_free(*scaled_frame->data);
        av_frame_free(&scaled_frame);
    }

    frame_and_packet_free(frame_and_packet);
    avcodec_free_context(&decoder);

    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);

    av_free(io_ctx->buffer);
    avio_context_free(&io_ctx);
    fclose(memfile.file);

    return TRUE;
}