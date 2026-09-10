#ifndef YULE_UI_GEOMETRY_H
#define YULE_UI_GEOMETRY_H

#include <math.h>

/* Keep drawing-space fractional bounds in the input path. Half-open edges give
 * adjacent controls one owner for a shared boundary; nonfinite geometry must
 * never turn a failed comparison into an all-screen input capture. */
static inline int ui_bounds_valid(float x, float y, float w, float h) {
    return isfinite(x) && isfinite(y) && isfinite(w) && isfinite(h) &&
           w > 0.0f && h > 0.0f;
}

static inline int ui_bounds_contains(float x, float y, float w, float h,
                                     int mouse_x, int mouse_y) {
    return ui_bounds_valid(x, y, w, h) &&
           (double)mouse_x >= (double)x && (double)mouse_y >= (double)y &&
           (double)mouse_x < (double)x + (double)w &&
           (double)mouse_y < (double)y + (double)h;
}

#endif
