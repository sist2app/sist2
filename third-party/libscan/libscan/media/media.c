#include "media.h"
#include "../ocr/ocr.h"
#include <ctype.h>

#define MIN_SIZE 32
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
void *scale_frame(const AVCodecContext *decoder, const AVFrame *frame, int size) {

    if (frame->pict_type == AV_PICTURE_TYPE_NONE) {
        return NULL;
    }

    int dstW;
    int dstH;
    if (frame->width <= size && frame->height <= size) {
        if (decoder->codec_id == AV_CODEC_ID_MJPEG || decoder->codec_id == AV_CODEC_ID_PNG) {
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
            decoder->width, decoder->height, decoder->pix_fmt,
            dstW, dstH, AV_PIX_FMT_YUV420P,
            SIST_SWS_ALGO, 0, 0, 0
    );

    int dst_buf_len = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, dstW, dstH, 1);
    uint8_t *dst_buf = (uint8_t *) av_malloc(dst_buf_len * 2);

    av_image_fill_arrays(scaled_frame->data, scaled_frame->linesize, dst_buf, AV_PIX_FMT_YUV420P, dstW, dstH, 1);

    sws_scale(sws_ctx,
              (const uint8_t *const *) frame->data, frame->linesize,
              0, decoder->height,
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
static void read_subtitles(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, int stream_idx, document_t *doc) {

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
            for (int i = 0; i < subtitle.num_rects; i++) {
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
    strncpy(dst, str, sizeof(dst)); \
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
        while ((tag = av_dict_get(frame->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
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
                APPEND_TAG_META(MetaExifUserComment);
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
    }
}

static void ocr_image_cb(const char *text, size_t len) {
    APPEND_STR_META(thread_doc, MetaContent, text);
}

#define OCR_PIXEL_FORMAT AV_PIX_FMT_RGB32
#define OCR_BYTES_PER_PIXEL 4
#define OCR_PIXELS_PER_INCH 70

void ocr_image(scan_media_ctx_t *ctx, document_t *doc, const AVCodecContext *decoder, AVFrame *frame) {

    // Convert to RGB32
    AVFrame *rgb_frame = av_frame_alloc();

    struct SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height, decoder->pix_fmt,
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
        ocr_image(ctx, doc, decoder, frame_and_packet->frame);
    }

    // NOTE: OCR'd content takes precedence over exif image description
    if (thumbnail_index == 0) {
        append_video_meta(ctx, pFormatCtx, frame_and_packet->frame, doc, IS_VIDEO(pFormatCtx));
    }

    // Scale frame
    AVFrame *scaled_frame = scale_frame(decoder, frame_and_packet->frame, ctx->tn_size);

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

        // Save thumbnail_count
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

void parse_media_format_ctx(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, document_t *doc) {

    int video_stream = -1;
    int audio_stream = -1;
    int subtitle_stream = -1;

    avformat_find_stream_info(pFormatCtx, NULL);

    for (int i = (int) pFormatCtx->nb_streams - 1; i >= 0; i--) {
        AVStream *stream = pFormatCtx->streams[i];

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
                                     // Limit to ~1 thumbnail_count every 7s
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

    return (ret == mem->size && mem->file != NULL) ? 0 : -1;
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

    if (f->st_size <= ctx->max_media_buffer) {
        int ret = memfile_open(f, &memfile);
        if (ret == 0) {
            CTX_LOG_DEBUGF(f->filepath, "Loading media file in memory (%ldB)", f->st_size);
            io_ctx = avio_alloc_context(buffer, AVIO_BUF_SIZE, 0, &memfile, memfile_read, NULL, memfile_seek);
        }
    }

    if (io_ctx == NULL) {
        CTX_LOG_DEBUGF(f->filepath, "Reading media file without seek support", f->st_size);
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
    AVFrame *scaled_frame = scale_frame(decoder, frame_and_packet->frame, ctx->tn_size);

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
        // Encode frame to jpeg
        AVCodecContext *jpeg_encoder = alloc_webp_encoder(scaled_frame->width, scaled_frame->height,
                                                          ctx->tn_qscale);
        avcodec_send_frame(jpeg_encoder, scaled_frame);
        avcodec_send_frame(jpeg_encoder, NULL); // Send EOF

        AVPacket *jpeg_packet = av_packet_alloc();
        avcodec_receive_packet(jpeg_encoder, jpeg_packet);

        // Save thumbnail_count
        doc->thumbnail_count = 1;
        APPEND_THUMBNAIL(doc, jpeg_packet->data, jpeg_packet->size);

        av_packet_free(&jpeg_packet);
        avcodec_free_context(&jpeg_encoder);
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