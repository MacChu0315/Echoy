#include "echoy_art_image.h"
#include "echoy_art.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ART_SIZE 480u
#define QOI_OVERHEAD 22u

static void write_be32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

/* GSP quantizes RGB888 with floor(channel * maximum / 255). Expand upward
 * so decoding the encoded art cannot lose one level from its RGB565 source. */
static uint8_t expand(unsigned channel, unsigned maximum)
{
    return (uint8_t)((channel * 255 + maximum - 1) / maximum);
}

bool echoy_art_encode_qoi(const echoy_art_frame_t *frame, uint8_t **out, size_t *size)
{
    if (out) {
        *out = NULL;
    }
    if (size) {
        *size = 0;
    }
    if (!frame || !out || !size || !frame->width || !frame->height || frame->width > MAX_ART_SIZE ||
        frame->height > MAX_ART_SIZE || frame->width > SIZE_MAX / frame->height) {
        return false;
    }
    const size_t count = (size_t)frame->width * frame->height;
    if (count > SIZE_MAX / sizeof(uint16_t) || count > (SIZE_MAX - QOI_OVERHEAD) / 4) {
        return false;
    }
    uint16_t *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) {
        return false;
    }
    uint8_t *encoded = malloc(count * 4 + QOI_OVERHEAD);
    if (!encoded) {
        free(pixels);
        return false;
    }
    echoy_art_frame_t snapshot = *frame;
    esp_gsp_canvas_surface_t surface = {
        .pixels = pixels,
        .stride_bytes = frame->width * sizeof(*pixels),
        .width = (uint16_t)frame->width,
        .height = (uint16_t)frame->height,
        .pixel_format = ESP_GSP_CANVAS_PIXEL_RGB565,
    };
    echoy_art_draw(&surface, &snapshot);

    /* QOI v1.0: https://qoiformat.org/qoi-specification.pdf. The opaque RGB
     * and RUN subset needs no color-index table or additional dependency. */
    memcpy(encoded, "qoif", 4);
    write_be32(encoded + 4, frame->width);
    write_be32(encoded + 8, frame->height);
    encoded[12] = 3; /* RGB */
    encoded[13] = 0; /* sRGB with linear alpha */
    size_t offset = 14;
    uint16_t previous = 0; /* The initial QOI pixel is opaque black. */
    unsigned run = 0;
    for (size_t i = 0; i < count; ++i) {
        uint16_t pixel = pixels[i];
        if (pixel == previous) {
            if (++run == 62) {
                encoded[offset++] = (uint8_t)(0xc0u | (run - 1));
                run = 0;
            }
        } else {
            if (run) {
                encoded[offset++] = (uint8_t)(0xc0u | (run - 1));
                run = 0;
            }
            encoded[offset++] = 0xfe;
            encoded[offset++] = expand((pixel >> 11) & 31, 31);
            encoded[offset++] = expand((pixel >> 5) & 63, 63);
            encoded[offset++] = expand(pixel & 31, 31);
            previous = pixel;
        }
    }
    if (run) {
        encoded[offset++] = (uint8_t)(0xc0u | (run - 1));
    }
    memset(encoded + offset, 0, 7);
    encoded[offset + 7] = 1;
    free(pixels);
    *out = encoded;
    *size = offset + 8;
    return true;
}
