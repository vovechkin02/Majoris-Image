#include <math.h>
#include <string.h>

#include <glycin.h>

// Glycin stores integer components in native byte order, with no alignment guarantee.
/** Reads a 16-bit sample from Glycin frame bytes with the correct layout. */
static inline guint16 read_sample16(const guint8 *data)
{
    guint16 value;
    memcpy(&value, data, sizeof value);
    return value;
}

/** Maps a normalized transfer-function sample into linear-light values. */
static double linear_sample(const guint8 *data, int depth)
{
    if (depth == 8) {
        return *data / 255.0;
    }
    return read_sample16(data) / 65535.0;
}

/** Selects the decoded transfer function for the frame color state. */
static inline double decode_transfer(double value, gboolean gray_gamma)
{
    if (gray_gamma) {
        return pow(value, 2.2);
    }
    if (value <= .04045) {
        return value / 12.92;
    }
    return pow((value + .055) / 1.055, 2.4);
}

/** Returns a lazily initialized lookup table for 8-bit transfer decoding. */
static const double *transfer_table(gboolean gray_gamma)
{
    static double srgb[256];
    static double gray[256];
    static gsize srgb_ready, gray_ready;
    double *table = gray_gamma ? gray : srgb;
    gsize *ready = gray_gamma ? &gray_ready : &srgb_ready;

    if (g_once_init_enter(ready)) {
        for (unsigned i = 0; i < 256; i++) {
            table[i] = decode_transfer(i / 255.0, gray_gamma);
        }
        g_once_init_leave(ready, 1);
    }
    return table;
}

/** Returns a lazily initialized lookup table for 16-bit transfer decoding. */
static const double *transfer_table16(gboolean gray_gamma)
{
    static double srgb[65536];
    static double gray[65536];
    static gsize srgb_ready, gray_ready;
    double *table = gray_gamma ? gray : srgb;
    gsize *ready = gray_gamma ? &gray_ready : &srgb_ready;

    // Publish only a complete table. Each transfer touches its own 512 KiB.
    if (g_once_init_enter(ready)) {
        for (unsigned i = 0; i < 65536; i++) {
            // Preserve the formula path's double precision through accumulation.
            table[i] = decode_transfer(i / 65535.0, gray_gamma);
        }
        g_once_init_leave(ready, 1);
    }
    return table;
}

/** Converts bounded Glycin frame rows into the ImageKit RGBA float layout. */
static GBytes *convert_pixels(GlyFrame *frame, int edge, double left, double top,
                              double region_width, double region_height, gboolean nearest,
                              gboolean gray_icc, gboolean linear_rgb, int *width, int *height,
                              int *depth, GCancellable *stop, GError **error)
{
    guint w = gly_frame_get_width(frame), h = gly_frame_get_height(frame);
    guint stride = gly_frame_get_stride(frame);
    GlyMemoryFormat format = gly_frame_get_memory_format(frame);
    int r = 0, g = 1, b = 2, a = -1, bands = 3;
    *depth = 8;
    switch (format) {
    case GLY_MEMORY_B8G8R8:
        r = 2;
        b = 0;
        break;
    case GLY_MEMORY_B8G8R8A8:
    case GLY_MEMORY_B8G8R8A8_PREMULTIPLIED:
        r = 2;
        b = 0;
        a = 3;
        bands = 4;
        break;
    case GLY_MEMORY_R8G8B8A8:
    case GLY_MEMORY_R8G8B8A8_PREMULTIPLIED:
        a = 3;
        bands = 4;
        break;
    case GLY_MEMORY_A8R8G8B8:
    case GLY_MEMORY_A8R8G8B8_PREMULTIPLIED:
        r = 1;
        g = 2;
        b = 3;
        a = 0;
        bands = 4;
        break;
    case GLY_MEMORY_A8B8G8R8:
        r = 3;
        g = 2;
        b = 1;
        a = 0;
        bands = 4;
        break;
    case GLY_MEMORY_R8G8B8:
        break;
    case GLY_MEMORY_R16G16B16:
        *depth = 16;
        break;
    case GLY_MEMORY_R16G16B16A16:
    case GLY_MEMORY_R16G16B16A16_PREMULTIPLIED:
        *depth = 16;
        a = 3;
        bands = 4;
        break;
    case GLY_MEMORY_G16:
        *depth = 16;
        G_GNUC_FALLTHROUGH;
    case GLY_MEMORY_G8:
        g = b = 0;
        bands = 1;
        break;
    case GLY_MEMORY_G16A16:
    case GLY_MEMORY_G16A16_PREMULTIPLIED:
        *depth = 16;
        G_GNUC_FALLTHROUGH;
    case GLY_MEMORY_G8A8:
    case GLY_MEMORY_G8A8_PREMULTIPLIED:
        g = b = 0;
        a = 1;
        bands = 2;
        break;
    default:
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Glycin color transfer or pixel layout is unavailable");
        return NULL;
    }
    int sample_depth = *depth;
    int component = sample_depth / 8, pixel = bands * component;
    gsize size;
    const guint8 *input = g_bytes_get_data(gly_frame_get_buf_bytes(frame), &size);
    if (!w || !h || edge < 1 || edge > 4096 || (guint64)w * h > 67108864 ||
        (guint64)w * pixel > stride || (guint64)(h - 1) * stride + (guint64)w * pixel > size) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid Glycin frame extent or stride");
        return NULL;
    }

    int x0 = MIN((int)w - 1, (int)floor(left * w + 1e-7));
    int y0 = MIN((int)h - 1, (int)floor(top * h + 1e-7));
    int crop_w = MAX(1, MIN((int)w - x0, (int)floor(region_width * w + 1e-7)));
    int crop_h = MAX(1, MIN((int)h - y0, (int)floor(region_height * h + 1e-7)));
    double scale = MIN(1.0, (double)edge / MAX(crop_w, crop_h));
    *width = MAX(1, (int)round(crop_w * scale));
    *height = MAX(1, (int)round(crop_h * scale));
    gsize count = (gsize)*width * *height * 4;
    float *out = g_try_new(float, count);
    if (!out) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Glycin preview memory limit");
        return NULL;
    }
    gboolean premultiplied = gly_memory_format_is_premultiplied(format);
    gboolean gray_gamma = gray_icc && r == g;
    const double *table = NULL;
    // Linear input needs no transfer. Unpremultiplication can land between entries.
    if (!premultiplied && !linear_rgb) {
        if (sample_depth == 8) {
            table = transfer_table(gray_gamma);
        } else {
            // Small conversions use the formula to avoid a cold 65,536-entry fill.
            gsize sampled_pixels = nearest ? count / 4 : (gsize)crop_w * crop_h;
            if (sampled_pixels >= 32768) {
                if (g_cancellable_set_error_if_cancelled(stop, error)) {
                    g_free(out);
                    return NULL;
                }
                table = transfer_table16(gray_gamma);
            }
        }
    }
    int channels[] = {r, g, b};
    double dx = (double)crop_w / *width, dy = (double)crop_h / *height;
    // Integrate the source-pixel area of each destination sample. This keeps
    // memory bounded for large fallback images and avoids dark alpha fringes.
    for (int y = 0; y < *height; y++) {
        double sy0 = y0 + y * dy;
        double sy1 = MIN(y0 + crop_h, sy0 + dy);
        if (nearest) {
            sy0 = floor(sy0);
            sy1 = sy0 + 1;
        }
        for (int x = 0; x < *width; x++) {
            double sx0 = x0 + x * dx;
            double sx1 = MIN(x0 + crop_w, sx0 + dx);
            if (nearest) {
                sx0 = floor(sx0);
                sx1 = sx0 + 1;
            }
            double sum[4] = {0};
            for (int sy = (int)sy0; sy < (int)ceil(sy1); sy++) {
                if (g_cancellable_set_error_if_cancelled(stop, error)) {
                    g_free(out);
                    return NULL;
                }
                double row_weight = MIN(sy + 1, sy1) - MAX(sy, sy0);
                for (int sx = (int)sx0; sx < (int)ceil(sx1); sx++) {
                    double area = row_weight * (MIN(sx + 1, sx1) - MAX(sx, sx0));
                    const guint8 *src = input + (gsize)sy * stride + (gsize)sx * pixel;
                    double alpha = a < 0 ? 1 : linear_sample(src + a * component, sample_depth);
                    if (!alpha) {
                        continue;
                    }
                    double weight = area * alpha;
                    if (table != NULL) {
                        if (sample_depth == 8) {
                            for (int c = 0; c < 3; c++) {
                                sum[c] += weight * table[src[channels[c]]];
                            }
                        } else {
                            for (int c = 0; c < 3; c++) {
                                sum[c] += weight * table[read_sample16(src + 2 * channels[c])];
                            }
                        }
                    } else {
                        for (int c = 0; c < 3; c++) {
                            double value =
                                linear_sample(src + channels[c] * component, sample_depth);
                            if (premultiplied) {
                                value = MIN(value / alpha, 1.0);
                            }
                            double linear = linear_rgb ? value : decode_transfer(value, gray_gamma);
                            sum[c] += weight * linear;
                        }
                    }
                    sum[3] += weight;
                }
            }
            float *dst = out + 4 * ((gsize)y * *width + x);
            for (int c = 0; c < 3; c++) {
                dst[c] = sum[3] > 0 ? sum[c] / sum[3] : 0;
            }
            dst[3] = CLAMP(sum[3] / ((sx1 - sx0) * (sy1 - sy0)), 0, 1);
        }
    }
    return g_bytes_new_take(out, count * sizeof(float));
}

/** Holds Glycin frame references and crop parameters for asynchronous conversion. */
typedef struct {
    GlyFrame *frame;
    int edge, width, height, depth;
    double left, top, region_width, region_height;
    gboolean nearest, gray_icc, linear_rgb;
} PixelJob;

/** Releases input references and output buffers owned by a conversion job. */
static void pixel_job_free(PixelJob *job)
{
    g_object_unref(job->frame);
    g_free(job);
}

/** Runs Glycin pixel conversion off the caller main context. */
static void convert_thread(GTask *task, gpointer source G_GNUC_UNUSED, gpointer data,
                           GCancellable *stop)
{
    PixelJob *job = data;
    GError *error = NULL;
    GBytes *pixels =
        convert_pixels(job->frame, job->edge, job->left, job->top, job->region_width,
                       job->region_height, job->nearest, job->gray_icc, job->linear_rgb,
                       &job->width, &job->height, &job->depth, stop, &error);
    if (pixels) {
        g_task_return_pointer(task, pixels, (GDestroyNotify)g_bytes_unref);
    } else {
        g_task_return_error(task, error);
    }
}

/** Begins asynchronous Glycin conversion with an explicit output profile. */
void image_glycin_pixels_profile_async(GlyFrame *frame, int edge, double left, double top,
                                       double region_width, double region_height, gboolean nearest,
                                       gboolean gray_icc, gboolean linear_rgb, GCancellable *stop,
                                       GAsyncReadyCallback callback, gpointer data)
{
    PixelJob *job = g_new0(PixelJob, 1);
    job->frame = g_object_ref(frame);
    job->edge = edge;
    job->left = left;
    job->top = top;
    job->region_width = region_width;
    job->region_height = region_height;
    job->nearest = nearest;
    job->gray_icc = gray_icc;
    job->linear_rgb = linear_rgb;
    GTask *task = g_task_new(NULL, stop, callback, data);
    g_task_set_task_data(task, job, (GDestroyNotify)pixel_job_free);
    g_task_run_in_thread(task, convert_thread);
    g_object_unref(task);
}

/** Begins asynchronous Glycin pixel conversion using the default color policy. */
void image_glycin_pixels_async(GlyFrame *frame, int edge, double left, double top,
                               double region_width, double region_height, gboolean nearest,
                               gboolean gray_icc, GCancellable *stop, GAsyncReadyCallback callback,
                               gpointer data)
{
    image_glycin_pixels_profile_async(frame, edge, left, top, region_width, region_height, nearest,
                                      gray_icc, FALSE, stop, callback, data);
}

/** Obtains the conversion result and propagates worker-thread errors. */
GBytes *image_glycin_pixels_finish(GAsyncResult *result, int *width, int *height, int *depth,
                                   GError **error)
{
    PixelJob *job = g_task_get_task_data(G_TASK(result));
    *width = job->width;
    *height = job->height;
    *depth = job->depth;
    return g_task_propagate_pointer(G_TASK(result), error);
}
