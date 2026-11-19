#include "lossless-jpeg.h"

#include <string.h>

#include <turbojpeg.h>

/* Compose EXIF's signed permutation with the requested clockwise quarter turns.
 * This never creates a pixel buffer, crops an edge, or quantizes coefficients. */
/**
 * Applies a perfect JPEG coefficient transform without recompression.
 * Rejects partial MCU transforms.
 */
gboolean
image_jpeg_transform(const char *source, const char *target, int orientation, int turns,
                     int *width, int *height, GError **error)
{
    static const int matrices[8][4] = {
        {1, 0, 0, 1}, {-1, 0, 0, 1}, {-1, 0, 0, -1}, {1, 0, 0, -1},
        {0, 1, 1, 0}, {0, -1, 1, 0}, {0, -1, -1, 0}, {0, 1, -1, 0}
    };
    static const int operations[8] = {
        TJXOP_NONE, TJXOP_HFLIP, TJXOP_ROT180, TJXOP_VFLIP,
        TJXOP_TRANSPOSE, TJXOP_ROT90, TJXOP_TRANSVERSE, TJXOP_ROT270
    };

    if (orientation < 1 || orientation > 8 || turns < 0 || turns > 3) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Invalid JPEG orientation");
        return FALSE;
    }

    int matrix[4];
    memcpy(matrix, matrices[orientation - 1], sizeof matrix);
    for (int i = 0; i < turns; i++) {
        int a = -matrix[2], b = -matrix[3];
        matrix[2] = matrix[0];
        matrix[3] = matrix[1];
        matrix[0] = a;
        matrix[1] = b;
    }

    tjtransform transform = {0};
    transform.options = TJXOPT_PERFECT;
    for (int i = 0; i < 8; i++) {
        if (!memcmp(matrix, matrices[i], sizeof matrix)) {
            transform.op = operations[i];
        }
    }

    gchar *input = NULL;
    gsize size = 0;
    if (!g_file_get_contents(source, &input, &size, error)) {
        return FALSE;
    }
    if (size == 0 || size > 268435456) {
        g_free(input);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "JPEG coefficient input exceeds worker limit");
        return FALSE;
    }

    tjhandle handle = tjInitTransform();
    unsigned char *output = NULL;
    unsigned long length = 0;
    int subsampling, colorspace;
    gboolean ok = FALSE;
    if (!handle || tjDecompressHeader3(handle, (unsigned char *)input, size, width, height,
                                      &subsampling, &colorspace) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid JPEG coefficient input");
    } else if (tjTransform(handle, (unsigned char *)input, size, 1, &output, &length,
                           &transform, TJFLAG_STOPONWARNING) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "A perfect lossless JPEG transform is unavailable; "
                            "source left unchanged");
    } else if (length == 0 || length > 268435456) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "JPEG coefficient output exceeds worker limit");
    } else if (tjDecompressHeader3(handle, output, length, width, height,
                                   &subsampling, &colorspace) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid JPEG coefficient output");
    } else {
        ok = g_file_set_contents(target, (const char *)output, length, error);
    }

    if (output) {
        tjFree(output);
    }
    if (handle) {
        tjDestroy(handle);
    }
    g_free(input);
    return ok;
}
