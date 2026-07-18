#pragma once

/* Pure window-size policy shared by the runtime and its focused tests.
 * Eggnogg's native window presets are all 3:2, and the renderer assumes that
 * aspect throughout its layout code. */

typedef struct WindowPolicySize {
    int w;
    int h;
} WindowPolicySize;

enum {
    WINDOW_POLICY_MIN_W = 480,
    WINDOW_POLICY_MIN_H = 320,
    WINDOW_POLICY_MID_W = 960,
    WINDOW_POLICY_MID_H = 640,
    WINDOW_POLICY_MAX_W = 1440,
    WINDOW_POLICY_MAX_H = 960,
    WINDOW_POLICY_FRAME_MARGIN_W = 48,
    WINDOW_POLICY_FRAME_MARGIN_H = 80,
};

static __inline int window_policy_min_int(int a, int b) {
    return a < b ? a : b;
}

static __inline int window_policy_max_fit_height(int display_w, int display_h) {
    int available_w = display_w - WINDOW_POLICY_FRAME_MARGIN_W;
    int available_h = display_h - WINDOW_POLICY_FRAME_MARGIN_H;
    int fit_h;

    if (available_w < WINDOW_POLICY_MIN_W) available_w = WINDOW_POLICY_MIN_W;
    if (available_h < WINDOW_POLICY_MIN_H) available_h = WINDOW_POLICY_MIN_H;
    if (available_w > WINDOW_POLICY_MAX_W) available_w = WINDOW_POLICY_MAX_W;
    if (available_h > WINDOW_POLICY_MAX_H) available_h = WINDOW_POLICY_MAX_H;

    fit_h = window_policy_min_int(available_h, (available_w * 2) / 3);
    if (fit_h > WINDOW_POLICY_MAX_H) fit_h = WINDOW_POLICY_MAX_H;
    if (fit_h < WINDOW_POLICY_MIN_H) fit_h = WINDOW_POLICY_MIN_H;
    fit_h &= ~1; /* even height makes width = height * 3 / 2 exact */
    if (fit_h < WINDOW_POLICY_MIN_H) fit_h = WINDOW_POLICY_MIN_H;
    return fit_h;
}

static __inline WindowPolicySize window_policy_fit_3_2(int requested_w,
                                                        int requested_h,
                                                        int display_w,
                                                        int display_h) {
    WindowPolicySize out;
    int desired_h;
    int max_h = window_policy_max_fit_height(display_w, display_h);

    if (requested_w <= 0) requested_w = WINDOW_POLICY_MID_W;
    if (requested_h <= 0) requested_h = WINDOW_POLICY_MID_H;

    /* Fit inside the requested rectangle instead of expanding a user drag in
     * the other dimension. This also makes snapped/maximized windows settle to
     * a predictable native-aspect size. */
    if ((long long)requested_w * 2 > (long long)requested_h * 3) {
        desired_h = requested_h;
    } else {
        desired_h = (requested_w * 2) / 3;
    }
    if (desired_h < WINDOW_POLICY_MIN_H) desired_h = WINDOW_POLICY_MIN_H;
    if (desired_h > max_h) desired_h = max_h;
    desired_h &= ~1;
    if (desired_h < WINDOW_POLICY_MIN_H) desired_h = WINDOW_POLICY_MIN_H;

    out.h = desired_h;
    out.w = (desired_h * 3) / 2;
    return out;
}

static __inline WindowPolicySize window_policy_largest_preset(int display_w,
                                                               int display_h) {
    WindowPolicySize out;
    int max_h = window_policy_max_fit_height(display_w, display_h);
    int max_w = (max_h * 3) / 2;

    if (max_w >= WINDOW_POLICY_MAX_W) {
        out.w = WINDOW_POLICY_MAX_W;
        out.h = WINDOW_POLICY_MAX_H;
    } else if (max_w >= WINDOW_POLICY_MID_W) {
        out.w = WINDOW_POLICY_MID_W;
        out.h = WINDOW_POLICY_MID_H;
    } else {
        out.w = WINDOW_POLICY_MIN_W;
        out.h = WINDOW_POLICY_MIN_H;
    }
    return out;
}

static __inline WindowPolicySize window_policy_next_preset(int current_w,
                                                            int display_w,
                                                            int display_h) {
    WindowPolicySize out;
    int max_h = window_policy_max_fit_height(display_w, display_h);
    int max_w = (max_h * 3) / 2;

    if (current_w < WINDOW_POLICY_MID_W && max_w >= WINDOW_POLICY_MID_W) {
        out.w = WINDOW_POLICY_MID_W;
        out.h = WINDOW_POLICY_MID_H;
    } else if (current_w < WINDOW_POLICY_MAX_W && max_w >= WINDOW_POLICY_MAX_W) {
        out.w = WINDOW_POLICY_MAX_W;
        out.h = WINDOW_POLICY_MAX_H;
    } else {
        out.w = WINDOW_POLICY_MIN_W;
        out.h = WINDOW_POLICY_MIN_H;
    }
    return out;
}
