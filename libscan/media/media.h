#ifndef SIST2_MEDIA_H
#define SIST2_MEDIA_H


#include "../scan.h"

#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"

typedef struct {
    log_callback_t log;
    logf_callback_t logf;

    int tn_size;
    int tn_qscale;
    /** Number of thumbnails to generate for videos */
    int tn_count;

    int64_t max_media_buffer;
    int read_subtitles;

    const char *tesseract_lang;
    const char *tesseract_path;

    /** Ogg containers are typed application/ogg, so their major mime says nothing about media */
    unsigned int ogg_mime;
} scan_media_ctx_t;

static inline AVCodecContext *alloc_jpeg_encoder(int w, int h, int qscale) {
    const AVCodec *jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext *jpeg = avcodec_alloc_context3(jpeg_codec);
    jpeg->width = w;
    jpeg->height = h;
    jpeg->time_base.den = 1000000;
    jpeg->time_base.num = 1;
    jpeg->i_quant_factor = (float) qscale;

    jpeg->pix_fmt = AV_PIX_FMT_YUVJ420P;
    int ret = avcodec_open2(jpeg, jpeg_codec, NULL);

    if (ret != 0) {
        return NULL;
    }

    return jpeg;
}

static inline AVCodecContext *alloc_webp_encoder(int w, int h, int qscale) {
    // AV_CODEC_ID_WEBP resolves to libwebp_anim, which wraps every still thumbnail in an
    // animation encoder for nothing
    const AVCodec *webp_codec = avcodec_find_encoder_by_name("libwebp");
    if (webp_codec == NULL) {
        webp_codec = avcodec_find_encoder(AV_CODEC_ID_WEBP);
    }

    AVCodecContext *webp = avcodec_alloc_context3(webp_codec);
    webp->width = w;
    webp->height = h;
    webp->time_base.den = 1000000;
    webp->time_base.num = 1;
    // libwebp method: 6 spends ~2x the CPU of 2 for ~5% fewer bytes, and thumbnail
    // encoding is the single most expensive step of a PDF scan
    webp->compression_level = 2;
    webp->global_quality = FF_QP2LAMBDA * qscale;

    webp->pix_fmt = AV_PIX_FMT_YUV420P;
    webp->color_range = AVCOL_RANGE_JPEG;
    int ret = avcodec_open2(webp, webp_codec, NULL);

    if (ret != 0) {
        return NULL;
    }

    return webp;
}


void parse_media(scan_media_ctx_t *ctx, vfile_t *f, document_t *doc, const char *mime_str);

void init_media();

/**
 * Re-encodes the picture of a file as WebP, scaled to fit max_size, for browsers that cannot
 * display the original format. Returns 0 and a buffer that the caller frees.
 */
int transcode_image(scan_media_ctx_t *ctx, const char *filepath, int max_size, void **buf, size_t *buf_len);

int store_image_thumbnail(scan_media_ctx_t *ctx, void *buf, size_t buf_len, document_t *doc, const char *url);

#endif
