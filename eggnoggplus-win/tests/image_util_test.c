#include "../image_util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const unsigned char source_2x2[] = {
        0, 10, 20, 255,   100, 20, 40, 255,
        200, 30, 60, 255,  255, 40, 80, 255
    };
    const unsigned char line[] = {
        0, 0, 0, 255,
        90, 30, 60, 255,
        180, 60, 120, 255
    };
    unsigned char pixel[4] = {0, 0, 0, 0};
    unsigned char blurred[sizeof(line)];
    unsigned char unchanged[4] = {1, 2, 3, 4};
    unsigned char* decoded = NULL;
    int decoded_width = 0;
    int decoded_height = 0;

    rgba_downsample_box(source_2x2, 2, 2, pixel, 1, 1);
    assert(pixel[0] == 138);
    assert(pixel[1] == 25);
    assert(pixel[2] == 50);
    assert(pixel[3] == 255);

    rgba_box_blur(line, blurred, 3, 1, 1);
    assert(blurred[0] == 30 && blurred[1] == 10 && blurred[2] == 20);
    assert(blurred[4] == 90 && blurred[5] == 30 && blurred[6] == 60);
    assert(blurred[8] == 150 && blurred[9] == 50 && blurred[10] == 100);
    assert(blurred[3] == 255 && blurred[7] == 255 && blurred[11] == 255);

    rgba_downsample_box(NULL, 1, 1, unchanged, 1, 1);
    assert(memcmp(unchanged, (unsigned char[]){1, 2, 3, 4}, 4) == 0);
    rgba_box_blur(unchanged, unchanged, 1, 1, 1);
    assert(memcmp(unchanged, (unsigned char[]){1, 2, 3, 4}, 4) == 0);

    /* Regression for large, indexed-color PNG sheets exported by ezgif. */
    assert(rgba_load_image_portable(
        "tests\\fixtures\\palette-grid-1060x910.png", &decoded,
        &decoded_width, &decoded_height));
    assert(decoded != NULL);
    assert(decoded_width == 1060 && decoded_height == 910);
    assert(decoded[3] == 0);
    free(decoded);

    puts("image utility tests: OK");
    return 0;
}
