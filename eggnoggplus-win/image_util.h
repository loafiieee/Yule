#ifndef EGGNOGGPLUS_IMAGE_UTIL_H
#define EGGNOGGPLUS_IMAGE_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Box-downsamples tightly packed RGBA8 pixels. Invalid dimensions are ignored. */
void rgba_downsample_box(const unsigned char* source,
                         int source_width,
                         int source_height,
                         unsigned char* destination,
                         int destination_width,
                         int destination_height);

/* Applies a clamped-edge RGBA8 box blur. Source and destination must be
 * separate buffers. Invalid dimensions or a nonpositive radius are ignored. */
void rgba_box_blur(const unsigned char* source,
                   unsigned char* destination,
                   int width,
                   int height,
                   int radius);

#ifdef __cplusplus
}
#endif

#endif
