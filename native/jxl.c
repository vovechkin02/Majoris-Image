#include <math.h>
#include <stdio.h>

#include <jxl/color_encoding.h>
#include <jxl/encode.h>

#include "bridge.h"

/** Encodes a JPEG XL image using the requested bit depth, quality and lossless policy. */
gboolean image_codec_jxl_encode(VipsImage *image, const char *path, int depth, int quality,
                                gboolean lossless, GError **error)
{
    VipsImage *linear = image_codec_to_linear(image, error);
    if (!linear) {
        return FALSE;
    }
    JxlEncoder *encoder = JxlEncoderCreate(NULL);
    void *pixels = NULL;
    FILE *file = NULL;
    gboolean ok = FALSE;
    if (!encoder) {
        goto fail;
    }
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = linear->Xsize;
    info.ysize = linear->Ysize;
    info.bits_per_sample = depth;
    info.exponent_bits_per_sample = depth == 16 ? 5 : 8;
    info.num_color_channels = 3;
    info.num_extra_channels = linear->Bands == 4 ? 1 : 0;
    if (linear->Bands == 4) {
        info.alpha_bits = depth;
        info.alpha_exponent_bits = info.exponent_bits_per_sample;
    }
    size_t size = 0;
    pixels = vips_image_write_to_memory(linear, &size);
    if (!pixels) {
        goto fail;
    }
    float peak = 1;
    const float *samples = pixels;
    for (size_t i = 0; i < size / sizeof(float); i++) {
        if (!isfinite(samples[i]) || (depth == 16 && fabsf(samples[i]) > 65504)) {
            goto fail;
        }
        if (i % linear->Bands < 3) {
            peak = fmaxf(peak, samples[i]);
        }
    }
    info.uses_original_profile = JXL_TRUE;
    info.intensity_target = 203 * peak;
    if (JxlEncoderSetBasicInfo(encoder, &info) != JXL_ENC_SUCCESS) {
        goto fail;
    }
    JxlColorEncoding color;
    JxlColorEncodingSetToLinearSRGB(&color, JXL_FALSE);
    if (JxlEncoderSetColorEncoding(encoder, &color) != JXL_ENC_SUCCESS) {
        goto fail;
    }
    JxlEncoderFrameSettings *settings = JxlEncoderFrameSettingsCreate(encoder, NULL);
    if (!settings || JxlEncoderSetFrameLossless(settings, lossless) != JXL_ENC_SUCCESS ||
        JxlEncoderSetFrameDistance(
            settings, lossless ? 0 : JxlEncoderDistanceFromQuality(quality)) != JXL_ENC_SUCCESS) {
        goto fail;
    }
    JxlPixelFormat format = {(uint32_t)linear->Bands, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
    if (JxlEncoderAddImageFrame(settings, &format, pixels, size) != JXL_ENC_SUCCESS) {
        goto fail;
    }
    // AddImageFrame copies the caller's pixels before returning.
    g_clear_pointer(&pixels, g_free);
    JxlEncoderCloseInput(encoder);
    file = fopen(path, "wb");
    if (!file) {
        goto fail;
    }
    JxlEncoderStatus status;
    do {
        guint8 buffer[65536];
        guint8 *next = buffer;
        size_t available = sizeof(buffer);
        status = JxlEncoderProcessOutput(encoder, &next, &available);
        if (status == JXL_ENC_ERROR ||
            fwrite(buffer, 1, sizeof(buffer) - available, file) != sizeof(buffer) - available) {
            goto fail;
        }
    } while (status == JXL_ENC_NEED_MORE_OUTPUT);
    if (status != JXL_ENC_SUCCESS || fflush(file) != 0) {
        goto fail;
    }
    ok = TRUE;
    goto done;
fail:
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "JPEG XL floating-point encoding failed");
done:
    if (file && fclose(file) != 0 && ok) {
        ok = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "JPEG XL output could not be closed");
    }
    if (encoder) {
        JxlEncoderDestroy(encoder);
    }
    g_free(pixels);
    g_object_unref(linear);
    return ok;
}
