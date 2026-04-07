/*
 * stb_image.h - Minimal stub for Photon
 *
 * Replace this with the real stb_image.h from:
 *   https://github.com/nothings/stb/blob/master/stb_image.h
 *
 * This stub declares the functions used by material.c so the project
 * structure compiles.  Link against the real library for actual image loading.
 */
#ifndef STB_IMAGE_H
#define STB_IMAGE_H

#include <stdint.h>

#ifndef STBIDEF
#define STBIDEF extern
#endif

STBIDEF uint8_t* stbi_load(const char* filename, int* x, int* y,
                           int* channels_in_file, int desired_channels);
STBIDEF void     stbi_image_free(void* retval_from_stbi_load);

#ifdef STB_IMAGE_IMPLEMENTATION

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Minimal placeholder implementation – loads raw uncompressed data only.
   Replace with the real stb_image.h for PNG/JPG/BMP/TGA support. */

STBIDEF uint8_t* stbi_load(const char* filename, int* x, int* y,
                           int* channels_in_file, int desired_channels)
{
    (void)desired_channels;

    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    /* Read a trivial raw header: width(4) height(4) channels(4) then pixels */
    uint32_t hdr[3];
    if (fread(hdr, sizeof(uint32_t), 3, f) != 3) { fclose(f); return NULL; }

    *x = (int)hdr[0];
    *y = (int)hdr[1];
    *channels_in_file = (int)hdr[2];

    int ch_out = desired_channels > 0 ? desired_channels : *channels_in_file;
    size_t src_size = (size_t)hdr[0] * hdr[1] * hdr[2];
    size_t dst_size = (size_t)hdr[0] * hdr[1] * (uint32_t)ch_out;

    uint8_t* src = (uint8_t*)malloc(src_size);
    uint8_t* dst = (uint8_t*)malloc(dst_size);
    if (!src || !dst) { free(src); free(dst); fclose(f); return NULL; }

    if (fread(src, 1, src_size, f) != src_size) {
        free(src); free(dst); fclose(f); return NULL;
    }
    fclose(f);

    /* Naive channel conversion */
    int src_ch = *channels_in_file;
    for (int i = 0; i < (int)(hdr[0] * hdr[1]); i++) {
        for (int c = 0; c < ch_out; c++) {
            if (c < src_ch)
                dst[i * ch_out + c] = src[i * src_ch + c];
            else
                dst[i * ch_out + c] = (c == 3) ? 255 : 0;
        }
    }
    free(src);
    return dst;
}

STBIDEF void stbi_image_free(void* retval_from_stbi_load)
{
    free(retval_from_stbi_load);
}

#endif /* STB_IMAGE_IMPLEMENTATION */

#endif /* STB_IMAGE_H */
