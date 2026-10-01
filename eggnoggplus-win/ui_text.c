#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef UI_TEXT_TEST
#include <stdio.h>
#endif
#include "ui_text.h"
#include "third_party/tandy2k/ui_fonts.h"

enum { ATLAS_W = 512, ATLAS_H = 512, CELL_W = 32, CELL_H = 48,
       COLUMNS = 16, FIRST_CHAR = 32, CHAR_COUNT = 96, MAX_LINES = 256,
       GLYPH_PAD = 2, RASTER_SCALE = 2 };
typedef struct UiTextLine {
    float x, y, scale, r, g, b, a;
    int align;
    UiTextStyle style;
    char text[1024];
} UiTextLine;
static UiTextLine g_lines[MAX_LINES];
static int g_line_count;
static GLuint g_texture;
static HGLRC g_context;
static float g_center_y;
static HANDLE g_font_resource;
static float g_glyph_left[CHAR_COUNT], g_glyph_width[CHAR_COUNT];
static int g_metrics_ready;

float ui_text_scale(float requested) {
    if (!isfinite(requested) || requested <= 0.0f) return 0.0f;
    return fminf(3.0f, fmaxf(0.75f, requested));
}
float ui_text_body_scale(float ui) { return fminf(1.875f, fmaxf(1.25f, 1.5f * ui)); }
float ui_text_caption_scale(float ui) { return fminf(1.625f, fmaxf(1.0f, 1.25f * ui)); }
float ui_text_title_scale(float ui) { return fminf(2.5f, fmaxf(1.5f, 1.875f * ui)); }

static int ui_text_prepare(void) {
    HDC dc;
    HFONT font = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ old_font = NULL, old_bitmap = NULL;
    BITMAPINFO info;
    TEXTMETRICA metrics;
    GLYPHMETRICS glyph;
    MAT2 transform = {{0,1},{0,0},{0,0},{0,1}};
    char resolved_face[64] = "";
    unsigned char* pixels = NULL;
    unsigned char* rgba = NULL;
    GLint old_texture;
    HGLRC context = wglGetCurrentContext();
    int i, ok = 0;
    if (!context) return 0;
    if (g_texture && g_context == context && glIsTexture(g_texture)) return 1;
    if (!g_font_resource) {
        DWORD count;
        g_font_resource = AddFontMemResourceEx((void*)g_ui_tandy_font,
            sizeof(g_ui_tandy_font), NULL, &count);
        if (!g_font_resource) return 0;
    }
    g_texture = 0;
    g_context = context;
    dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = ATLAS_W;
    info.bmiHeader.biHeight = -ATLAS_H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    rgba = (unsigned char*)malloc(ATLAS_W * ATLAS_H * 4u);
    font = CreateFontA(-16 * RASTER_SCALE, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Px437 Tandy2K");
    if (!font || !bitmap || !pixels || !rgba) goto cleanup;
    old_bitmap = SelectObject(dc, bitmap);
    old_font = SelectObject(dc, font);
    if (!GetTextMetricsA(dc, &metrics) ||
        !GetTextFaceA(dc, sizeof(resolved_face), resolved_face) ||
        strcmp(resolved_face, "Px437 Tandy2K") != 0 ||
        GetGlyphOutlineA(dc, 'H', GGO_METRICS, &glyph, 0, NULL, &transform) == GDI_ERROR)
        goto cleanup;
    g_center_y = (GLYPH_PAD + metrics.tmAscent - glyph.gmptGlyphOrigin.y +
        glyph.gmBlackBoxY * 0.5f) / RASTER_SCALE;
    memset(pixels, 0, ATLAS_W * ATLAS_H * 4u);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < CHAR_COUNT; i++) {
        char character = (char)(FIRST_CHAR + i);
        RECT clip = { (i % COLUMNS) * CELL_W, (i / COLUMNS) * CELL_H,
                      (i % COLUMNS + 1) * CELL_W, (i / COLUMNS + 1) * CELL_H };
        if (!ExtTextOutA(dc, clip.left + GLYPH_PAD, clip.top + GLYPH_PAD,
            ETO_CLIPPED, &clip, &character, 1, NULL)) goto cleanup;
    }
    GdiFlush();
    /* Menus use the actual ink bounds, so narrow letters don't leave large
     * gaps. The console keeps the font's original fixed eight-pixel cells. */
    for (i = 0; i < CHAR_COUNT; i++) {
        int left = CELL_W, right = -1;
        int cell_x = (i % COLUMNS) * CELL_W, cell_y = (i / COLUMNS) * CELL_H;
        for (int y = 0; y < CELL_H; y++) for (int x = 0; x < CELL_W; x++) {
            if (pixels[((cell_y + y) * ATLAS_W + cell_x + x) * 4]) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        g_glyph_left[i] = right >= left ? (float)left / RASTER_SCALE : 0;
        g_glyph_width[i] = right >= left ? (float)(right - left + 1) / RASTER_SCALE : 4;
    }
    g_metrics_ready = 1;
    for (i = 0; i < ATLAS_W * ATLAS_H; i++) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = pixels[i * 4];
    }
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    glPopClientAttrib();
    ok = g_texture && glIsTexture(g_texture);
cleanup:
#ifdef UI_TEXT_TEST
    if (!ok) fprintf(stderr, "UI font preparation failed: face=%s error=%lu\n",
                     resolved_face, (unsigned long)GetLastError());
#endif
    if (old_font) SelectObject(dc, old_font);
    if (old_bitmap) SelectObject(dc, old_bitmap);
    if (font) DeleteObject(font);
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(dc);
    free(rgba);
    return ok;
}

int ui_text_queue_style(float x, float y, float scale, float r, float g, float b,
                        float a, const char* text, int align, UiTextStyle style) {
    UiTextLine* line;
    size_t length;
    if (style < 0 || style >= UI_TEXT_STYLE_COUNT) return 0;
    scale = ui_text_scale(scale);
    if (!text || !text[0] || !scale || a <= 0.0f) return 1;
    length = strlen(text);
    if (g_line_count == MAX_LINES || length >= sizeof(g_lines[0].text) ||
        !ui_text_prepare()) return 0;
    line = &g_lines[g_line_count++];
    line->x = x; line->y = y; line->scale = scale;
    line->r = r; line->g = g; line->b = b; line->a = a; line->align = align;
    line->style = style;
    memcpy(line->text, text, length + 1);
    return 1;
}

int ui_text_queue(float x, float y, float scale, float r, float g, float b,
                  float a, const char* text, int align) {
    return ui_text_queue_style(x,y,scale,r,g,b,a,text,align,UI_TEXT_BODY);
}
int ui_text_queue_mono(float x, float y, float scale, float r, float g, float b,
                       float a, const char* text, int align) {
    return ui_text_queue_style(x,y,scale,r,g,b,a,text,align,UI_TEXT_MONO);
}
float ui_text_width(const char* text, float scale) {
    return ui_text_style_width(text, scale, UI_TEXT_BODY);
}
float ui_text_style_width(const char* text, float scale, UiTextStyle style) {
    float width = 0;
    const unsigned char* p = (const unsigned char*)text;
    if (!text || style < 0 || style >= UI_TEXT_STYLE_COUNT) return 0;
    if (style == UI_TEXT_MONO || (!g_metrics_ready && !ui_text_prepare()))
        return (float)strlen(text) * UI_TEXT_ADVANCE * ui_text_scale(scale);
    for (; *p; p++) {
        unsigned int index = *p >= FIRST_CHAR && *p < FIRST_CHAR + CHAR_COUNT
            ? *p - FIRST_CHAR : '?' - FIRST_CHAR;
        width += g_glyph_width[index] + (p[1] ? 1.0f : 0.0f);
    }
    return width * ui_text_scale(scale);
}

void ui_text_fit(char* text, float width, float scale) {
    size_t length;
    if (!text) return;
    length = strlen(text);
    if (ui_text_width(text, scale) <= width) return;
    while (length > 3) {
        memcpy(text + length - 3, "...", 4);
        if (ui_text_width(text, scale) <= width) return;
        length--;
    }
    text[0] = '\0';
}

static void ui_text_draw_line(const UiTextLine* line, float offset, int shadow) {
    const unsigned char* p = (const unsigned char*)line->text;
    float x = line->x;
    float y = line->y - g_center_y * line->scale;
    if (line->align == 1) x -= ui_text_style_width(line->text, line->scale, line->style) * 0.5f;
    if (line->align == 2) x -= ui_text_style_width(line->text, line->scale, line->style);
    x = floorf(x + 0.5f) + offset;
    y = floorf(y + 0.5f) + offset;
    if (shadow) glColor4f(0.0f, 0.0f, 0.0f, line->a);
    else glColor4f(line->r, line->g, line->b, line->a);
    for (; *p; p++) {
        unsigned int index = *p >= FIRST_CHAR && *p < FIRST_CHAR + CHAR_COUNT
            ? *p - FIRST_CHAR : '?' - FIRST_CHAR;
        float u = (float)((index % COLUMNS) * CELL_W) / ATLAS_W;
        float v = (float)((index / COLUMNS) * CELL_H) / ATLAS_H;
        float u1 = u + (float)CELL_W / ATLAS_W;
        float v1 = v + (float)CELL_H / ATLAS_H;
        float left = x - (line->style == UI_TEXT_MONO ? (float)GLYPH_PAD / RASTER_SCALE
                            : g_glyph_left[index]) * line->scale;
        float right = left + (float)CELL_W / RASTER_SCALE * line->scale;
        float bottom = y + (float)CELL_H / RASTER_SCALE * line->scale;
        glTexCoord2f(u, v); glVertex2f(left, y);
        glTexCoord2f(u1, v); glVertex2f(right, y);
        glTexCoord2f(u1, v1); glVertex2f(right, bottom);
        glTexCoord2f(u, v1); glVertex2f(left, bottom);
        x += (line->style == UI_TEXT_MONO ? UI_TEXT_ADVANCE : g_glyph_width[index] + 1.0f) * line->scale;
    }
}

void ui_text_flush(float width, float height) {
    GLint matrix_mode;
    int i;
    if (!g_line_count) return;
    if (width <= 0 || height <= 0 || !ui_text_prepare()) {
        g_line_count = 0;
        return;
    }
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (i = 0; i < g_line_count; i++) {
        /* A 2x source atlas gives consistent strokes at intermediate sizes;
         * whole-pixel sizes retain the original hard pixel edges. */
        GLint filter = fabsf(g_lines[i].scale - roundf(g_lines[i].scale)) < 0.001f
            ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        /* Match the old font's small black shadow. Keep the offset on whole
         * screen pixels and fade it with the text; layout measures only ink. */
        float shadow_offset = fmaxf(1.0f, roundf(g_lines[i].scale));
        glBegin(GL_QUADS);
        /* Keep dark labels on the bright selected buttons crisp: another
         * black silhouette would merge their strokes. */
        if (fmaxf(g_lines[i].r, fmaxf(g_lines[i].g, g_lines[i].b)) > 0.25f)
            ui_text_draw_line(&g_lines[i], shadow_offset, 1);
        ui_text_draw_line(&g_lines[i], 0.0f, 0);
        glEnd();
    }
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(matrix_mode); glPopAttrib();
    g_line_count = 0;
}

void ui_text_shutdown(void) {
    if (g_texture && g_context == wglGetCurrentContext()) glDeleteTextures(1, &g_texture);
    g_texture = 0; g_context = NULL; g_line_count = 0;
    if (g_font_resource) RemoveFontMemResourceEx(g_font_resource);
    g_font_resource = NULL;
}
