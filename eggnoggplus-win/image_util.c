#include "image_util.h"

#include <stddef.h>
#include <stdint.h>

void rgba_downsample_box(const unsigned char* source,
                         int source_width,
                         int source_height,
                         unsigned char* destination,
                         int destination_width,
                         int destination_height) {
    int y;
    int x;

    if (!source || !destination || source_width <= 0 || source_height <= 0 ||
        destination_width <= 0 || destination_height <= 0) {
        return;
    }

    for (y = 0; y < destination_height; ++y) {
        int source_y0 = (y * source_height) / destination_height;
        int source_y1 = ((y + 1) * source_height) / destination_height;
        if (source_y1 <= source_y0) source_y1 = source_y0 + 1;
        if (source_y1 > source_height) source_y1 = source_height;

        for (x = 0; x < destination_width; ++x) {
            int source_x0 = (x * source_width) / destination_width;
            int source_x1 = ((x + 1) * source_width) / destination_width;
            uint64_t totals[4] = {0, 0, 0, 0};
            uint64_t count = 0;
            int source_y;
            int source_x;
            size_t destination_offset;

            if (source_x1 <= source_x0) source_x1 = source_x0 + 1;
            if (source_x1 > source_width) source_x1 = source_width;

            for (source_y = source_y0; source_y < source_y1; ++source_y) {
                const unsigned char* row =
                    source + (size_t)source_y * (size_t)source_width * 4u;
                for (source_x = source_x0; source_x < source_x1; ++source_x) {
                    const unsigned char* pixel =
                        row + (size_t)source_x * 4u;
                    totals[0] += pixel[0];
                    totals[1] += pixel[1];
                    totals[2] += pixel[2];
                    totals[3] += pixel[3];
                    ++count;
                }
            }

            if (count == 0) count = 1;
            destination_offset =
                ((size_t)y * (size_t)destination_width + (size_t)x) * 4u;
            destination[destination_offset] =
                (unsigned char)(totals[0] / count);
            destination[destination_offset + 1u] =
                (unsigned char)(totals[1] / count);
            destination[destination_offset + 2u] =
                (unsigned char)(totals[2] / count);
            destination[destination_offset + 3u] =
                (unsigned char)(totals[3] / count);
        }
    }
}

void rgba_box_blur(const unsigned char* source,
                   unsigned char* destination,
                   int width,
                   int height,
                   int radius) {
    int y;
    int x;

    if (!source || !destination || source == destination || width <= 0 ||
        height <= 0 || radius <= 0) {
        return;
    }

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint64_t totals[4] = {0, 0, 0, 0};
            uint64_t count = 0;
            int kernel_y;
            size_t destination_offset;

            for (kernel_y = -radius; kernel_y <= radius; ++kernel_y) {
                int source_y = y + kernel_y;
                int kernel_x;
                if (source_y < 0) source_y = 0;
                if (source_y >= height) source_y = height - 1;

                for (kernel_x = -radius; kernel_x <= radius; ++kernel_x) {
                    int source_x = x + kernel_x;
                    const unsigned char* pixel;
                    if (source_x < 0) source_x = 0;
                    if (source_x >= width) source_x = width - 1;
                    pixel = source +
                        ((size_t)source_y * (size_t)width +
                         (size_t)source_x) * 4u;
                    totals[0] += pixel[0];
                    totals[1] += pixel[1];
                    totals[2] += pixel[2];
                    totals[3] += pixel[3];
                    ++count;
                }
            }

            if (count == 0) count = 1;
            destination_offset =
                ((size_t)y * (size_t)width + (size_t)x) * 4u;
            destination[destination_offset] =
                (unsigned char)(totals[0] / count);
            destination[destination_offset + 1u] =
                (unsigned char)(totals[1] / count);
            destination[destination_offset + 2u] =
                (unsigned char)(totals[2] / count);
            destination[destination_offset + 3u] =
                (unsigned char)(totals[3] / count);
        }
    }
}
