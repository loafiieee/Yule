#include <assert.h>
#include <stdio.h>

#include "../window_policy.h"

static void assert_native(WindowPolicySize size) {
    assert(size.w >= WINDOW_POLICY_MIN_W);
    assert(size.h >= WINDOW_POLICY_MIN_H);
    assert(size.w <= WINDOW_POLICY_MAX_W);
    assert(size.h <= WINDOW_POLICY_MAX_H);
    assert(size.w * 2 == size.h * 3);
}

int main(void) {
    WindowPolicySize size;

    size = window_policy_fit_3_2(1200, 700, 1920, 1080);
    assert(size.w == 1050 && size.h == 700);
    size = window_policy_fit_3_2(800, 700, 1920, 1080);
    assert(size.w == 798 && size.h == 532);
    size = window_policy_fit_3_2(2000, 2000, 1920, 1080);
    assert(size.w == 1440 && size.h == 960);
    size = window_policy_fit_3_2(100, 100, 800, 600);
    assert(size.w == 480 && size.h == 320);

    size = window_policy_next_preset(480, 1920, 1080);
    assert(size.w == 960 && size.h == 640);
    size = window_policy_next_preset(960, 1920, 1080);
    assert(size.w == 1440 && size.h == 960);
    size = window_policy_next_preset(1440, 1920, 1080);
    assert(size.w == 480 && size.h == 320);
    size = window_policy_next_preset(960, 1366, 768);
    assert(size.w == 480 && size.h == 320);
    size = window_policy_next_preset(480, 1366, 768);
    assert(size.w == 960 && size.h == 640);
    size = window_policy_next_preset(480, 800, 600);
    assert(size.w == 480 && size.h == 320);

    /* Maximize is deliberately normalized to one of the three presets; it
     * never retains the platform maximized state or an arbitrary 3:2 size. */
    size = window_policy_largest_preset(1920, 1080);
    assert(size.w == 1440 && size.h == 960);
    size = window_policy_largest_preset(1366, 768);
    assert(size.w == 960 && size.h == 640);
    size = window_policy_largest_preset(800, 600);
    assert(size.w == 480 && size.h == 320);

    for (int w = 1; w <= 2400; w += 37) {
        for (int h = 1; h <= 1600; h += 29) {
            size = window_policy_fit_3_2(w, h, 1920, 1080);
            assert_native(size);
        }
    }

    puts("window policy tests: OK");
    return 0;
}
