#pragma once

/* Bundled Tandy2K menu and console text.
 * Coordinates specify the center of the capital-letter height. */
#define UI_TEXT_ADVANCE 8.0f
#define UI_TEXT_HEIGHT 16.0f
#define UI_TEXT_CONSOLE_SCALE 1.5f
typedef enum UiTextStyle {
    UI_TEXT_BODY, UI_TEXT_EMPHASIS, UI_TEXT_MONO, UI_TEXT_STYLE_COUNT
} UiTextStyle;
int ui_text_queue_style(float x, float y, float scale, float r, float g, float b,
                        float a, const char* text, int align, UiTextStyle style);
int ui_text_queue(float x, float y, float scale, float r, float g, float b,
                  float a, const char* text, int align);
int ui_text_queue_mono(float x, float y, float scale, float r, float g, float b,
                       float a, const char* text, int align);
float ui_text_width(const char* text, float scale);
float ui_text_style_width(const char* text, float scale, UiTextStyle style);
float ui_text_scale(float requested);
float ui_text_body_scale(float ui);
float ui_text_caption_scale(float ui);
float ui_text_title_scale(float ui);
void ui_text_fit(char* text, float width, float scale);
void ui_text_flush(float width, float height);
void ui_text_shutdown(void);
