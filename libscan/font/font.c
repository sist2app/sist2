#include "font.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include "../util.h"


__thread FT_Library ft_lib = NULL;


typedef struct text_dimensions {
    unsigned int width;
    unsigned int height;
    unsigned int baseline;
} text_dimensions_t;

typedef struct glyph {
    int top;
    int height;
    int width;
    int descent;
    int ascent;
    int advance_width;
    unsigned char *pixmap;
} glyph_t;


__always_inline
int kerning_offset(char c, char pc, FT_Face face) {
    FT_Vector kerning;
    FT_Get_Kerning(face, c, pc, FT_KERNING_DEFAULT, &kerning);

    return (int) (kerning.x / 64);
}

__always_inline
glyph_t ft_glyph_to_glyph(FT_GlyphSlot slot) {
    glyph_t glyph;

    glyph.pixmap = slot->bitmap.buffer;

    glyph.width = (int) slot->bitmap.width;
    glyph.height = (int) slot->bitmap.rows;
    glyph.top = slot->bitmap_top;
    glyph.advance_width = (int) slot->advance.x / 64;

    glyph.descent = MAX(0, glyph.height - glyph.top);
    glyph.ascent = MAX(0, MAX(glyph.top, glyph.height) - glyph.descent);

    return glyph;
}

text_dimensions_t text_dimension(char *text, FT_Face face) {
    text_dimensions_t dimensions;

    dimensions.width = 0;

    int num_chars = (int) strlen(text);

    unsigned int max_ascent = 0;
    int max_descent = 0;

    char pc = 0;
    for (int i = 0; i < num_chars; i++) {
        char c = text[i];

        FT_Load_Char(face, c, 0);
        glyph_t glyph = ft_glyph_to_glyph(face->glyph);

        max_descent = MAX(max_descent, glyph.descent);
        max_ascent = MAX(max_ascent, MAX(glyph.height, glyph.ascent));

        int kerning_x = kerning_offset(c, pc, face);
        dimensions.width += MAX(glyph.advance_width, glyph.width) + kerning_x;

        pc = c;
    }

    dimensions.height = max_ascent + max_descent;
    dimensions.baseline = max_descent;

    return dimensions;
}

void draw_glyph(glyph_t *glyph, int x, int y, struct text_dimensions text_info, unsigned char *bitmap) {
    unsigned int src = 0;
    unsigned int dst = y * text_info.width + x;
    unsigned int row_offset = text_info.width - glyph->width;
    unsigned int buf_len = text_info.width * text_info.height;

    for (unsigned int sy = 0; sy < glyph->height; sy++) {
        for (unsigned int sx = 0; sx < glyph->width; sx++) {
            if (dst < buf_len) {
                bitmap[dst] |= glyph->pixmap[src];
            }
            src++;
            dst++;
        }
        dst += row_offset;
    }
}

void bmp_format(dyn_buffer_t *buf, text_dimensions_t dimensions, const unsigned char *bitmap) {

    dyn_buffer_write_short(buf, 0x4D42); // Magic
    dyn_buffer_write_int(buf, 0); // Size placeholder
    dyn_buffer_write_int(buf, 0x5157); //Reserved
    dyn_buffer_write_int(buf, 14 + 40 + 256 * 4); // pixels offset

    dyn_buffer_write_int(buf, 40); // DIB size
    dyn_buffer_write_int(buf, (int) dimensions.width);
    dyn_buffer_write_int(buf, (int) dimensions.height);
    dyn_buffer_write_short(buf, 1); // Color planes
    dyn_buffer_write_short(buf, 8); // bits per pixel
    dyn_buffer_write_int(buf, 0); // compression
    dyn_buffer_write_int(buf, 0); // Ignored
    dyn_buffer_write_int(buf, 3800); // hres
    dyn_buffer_write_int(buf, 3800); // vres
    dyn_buffer_write_int(buf, 256); // Color count
    dyn_buffer_write_int(buf, 0); // Ignored

    // RGBA32 Color table (Grayscale)
    for (int i = 255; i >= 0; i--) {
        dyn_buffer_write_int(buf, i + (i << 8) + (i << 16));
    }

    // Pixel array: write from bottom to top, with rows padded to multiples of 4-bytes
    for (int y = (int) dimensions.height - 1; y >= 0; y--) {
        for (unsigned int x = 0; x < dimensions.width; x++) {
            dyn_buffer_write_char(buf, (char) bitmap[y * dimensions.width + x]);
        }
        while (buf->cur % 4 != 0) {
            dyn_buffer_write_char(buf, 0);
        }
    }

    // Size
    *(int *) ((char *) buf->buf + 2) = buf->cur;
}

void parse_font(scan_font_ctx_t *ctx, vfile_t *f, document_t *doc) {
    if (ft_lib == NULL) {
        FT_Init_FreeType(&ft_lib);
    }

    size_t buf_len = 0;
    void *buf = read_all(f, &buf_len);
    if (buf == NULL) {
        CTX_LOG_ERROR(f->filepath, "read_all() failed");
        return;
    }

    FT_Face face;
    FT_Error err = FT_New_Memory_Face(ft_lib, (unsigned char *) buf, (int) buf_len, 0, &face);
    if (err != 0) {
        CTX_LOG_ERRORF(doc->filepath, "(font.c) FT_New_Memory_Face() returned error code [%d] %s", err,
                       FT_Error_String(err));
        free(buf);
        return;
    }

    char font_name[4096];

    if (face->style_name == NULL || (strcmp(face->style_name, "?") == 0)) {
        if (face->family_name == NULL) {
            strcpy(font_name, "(null)");
        } else {
            strncpy(font_name, face->family_name, sizeof(font_name));
        }
    } else {
        snprintf(font_name, sizeof(font_name), "%s %s", face->family_name, face->style_name);
    }

    meta_line_t *meta_name = malloc(sizeof(meta_line_t) + strlen(font_name));
    meta_name->key = MetaFontName;
    strcpy(meta_name->str_val, font_name);
    APPEND_META(doc, meta_name);

    if (!ctx->enable_tn) {
        FT_Done_Face(face);
        free(buf);
        return;
    }

    int pixel = 64;
    int num_chars = (int) strlen(font_name);

    err = FT_Set_Pixel_Sizes(face, 0, pixel);
    if (err != 0) {
        CTX_LOG_WARNINGF(doc->filepath, "(font.c) FT_Set_Pixel_Sizes() returned error code [%d] %s", err,
                         FT_Error_String(err));
        FT_Done_Face(face);
        free(buf);
        return;
    }

    text_dimensions_t dimensions = text_dimension(font_name, face);
    unsigned char *bitmap = calloc(dimensions.width * dimensions.height, 1);

    FT_Vector pen;
    pen.x = 0;

    char pc = 0;
    for (int i = 0; i < num_chars; i++) {
        char c = font_name[i];

        err = FT_Load_Char(face, c, FT_LOAD_NO_HINTING | FT_LOAD_RENDER);
        if (err != 0) {
            c = c >= 'a' && c <= 'z' ? c - 32 : c + 32;
            err = FT_Load_Char(face, c, FT_LOAD_NO_HINTING | FT_LOAD_RENDER);
            if (err != 0) {
                CTX_LOG_WARNINGF(doc->filepath, "(font.c) FT_Load_Char() returned error code [%d] %s", err,
                                 FT_Error_String(err));
                continue;
            }
        }
        glyph_t glyph = ft_glyph_to_glyph(face->glyph);

        pen.x += kerning_offset(c, pc, face);
        if (pen.x <= 0) {
            pen.x = ABS(glyph.advance_width - glyph.width);
        }
        pen.y = dimensions.height - glyph.ascent - dimensions.baseline;

        draw_glyph(&glyph, pen.x, pen.y, dimensions, bitmap);

        pen.x += glyph.advance_width;
        pc = c;
    }

    dyn_buffer_t bmp_data = dyn_buffer_create();
    bmp_format(&bmp_data, dimensions, bitmap);

    doc->thumbnail_count = 1;
    APPEND_THUMBNAIL(doc, bmp_data.buf, bmp_data.cur);

    dyn_buffer_destroy(&bmp_data);
    free(bitmap);

    FT_Done_Face(face);
    free(buf);
}

void cleanup_font() {
    FT_Done_FreeType(ft_lib);
}
