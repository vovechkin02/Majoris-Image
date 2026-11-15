#include <glycin.h>
#include <math.h>
#include <string.h>

static double linear_sample(const guint8 *data, int depth)
{
    guint16 value;
    if (depth == 8) return *data / 255.0;
    memcpy(&value, data, sizeof value);
    return value / 65535.0;
}

static GBytes *convert_pixels(GlyFrame *frame, int edge, double left, double top,
                              double region_width, double region_height, gboolean nearest,
                              gboolean gray_icc, gboolean linear_rgb, int *width, int *height, int *depth,
                              GCancellable *stop, GError **error)
{
    guint w = gly_frame_get_width(frame), h = gly_frame_get_height(frame);
    guint stride = gly_frame_get_stride(frame);
    GlyMemoryFormat format = gly_frame_get_memory_format(frame);
    int r = 0, g = 1, b = 2, a = -1, bands = 3;
    *depth = 8;
    switch (format) {
    case GLY_MEMORY_B8G8R8: r = 2; b = 0; break;
    case GLY_MEMORY_B8G8R8A8:
    case GLY_MEMORY_B8G8R8A8_PREMULTIPLIED: r = 2; b = 0; a = 3; bands = 4; break;
    case GLY_MEMORY_R8G8B8A8:
    case GLY_MEMORY_R8G8B8A8_PREMULTIPLIED: a = 3; bands = 4; break;
    case GLY_MEMORY_A8R8G8B8:
    case GLY_MEMORY_A8R8G8B8_PREMULTIPLIED: r = 1; g = 2; b = 3; a = 0; bands = 4; break;
    case GLY_MEMORY_A8B8G8R8: r = 3; g = 2; b = 1; a = 0; bands = 4; break;
    case GLY_MEMORY_R8G8B8: break;
    case GLY_MEMORY_R16G16B16: *depth = 16; break;
    case GLY_MEMORY_R16G16B16A16:
    case GLY_MEMORY_R16G16B16A16_PREMULTIPLIED: *depth = 16; a = 3; bands = 4; break;
    case GLY_MEMORY_G16: *depth = 16; G_GNUC_FALLTHROUGH;
    case GLY_MEMORY_G8: g = b = 0; bands = 1; break;
    case GLY_MEMORY_G16A16:
    case GLY_MEMORY_G16A16_PREMULTIPLIED: *depth = 16; G_GNUC_FALLTHROUGH;
    case GLY_MEMORY_G8A8:
    case GLY_MEMORY_G8A8_PREMULTIPLIED: g = b = 0; a = 1; bands = 2; break;
    default:
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Glycin color transfer or pixel layout is unavailable");
        return NULL;
    }
    int component = *depth / 8, pixel = bands * component;
    gsize size;
    const guint8 *input = g_bytes_get_data(gly_frame_get_buf_bytes(frame), &size);
    if (!w || !h || edge < 1 || edge > 4096 || (guint64)w * h > 67108864 ||
        (guint64)w * pixel > stride || (guint64)(h - 1) * stride + (guint64)w * pixel > size) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid Glycin frame extent or stride");
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
    int channels[] = {r, g, b};
    double dx = (double)crop_w / *width, dy = (double)crop_h / *height;
    // Integrate the source-pixel area of each destination sample. This keeps
    // memory bounded for large fallback images and avoids dark alpha fringes.
    for (int y = 0; y < *height; y++) {
        double sy0 = y0 + y * dy;
        double sy1 = MIN(y0 + crop_h, sy0 + dy);
        if (nearest) { sy0 = floor(sy0); sy1 = sy0 + 1; }
        for (int x = 0; x < *width; x++) {
            double sx0 = x0 + x * dx;
            double sx1 = MIN(x0 + crop_w, sx0 + dx);
            if (nearest) { sx0 = floor(sx0); sx1 = sx0 + 1; }
            double sum[4] = {0};
            for (int sy = (int)sy0; sy < (int)ceil(sy1); sy++) {
                if (g_cancellable_set_error_if_cancelled(stop, error)) { g_free(out); return NULL; }
                double row_weight = MIN(sy + 1, sy1) - MAX(sy, sy0);
                for (int sx = (int)sx0; sx < (int)ceil(sx1); sx++) {
                    double area = row_weight * (MIN(sx + 1, sx1) - MAX(sx, sx0));
                    const guint8 *src = input + (gsize)sy * stride + (gsize)sx * pixel;
                    double alpha = a < 0 ? 1 : linear_sample(src + a * component, *depth);
                    if (!alpha) continue;
                    double weight = area * alpha;
                    for (int c = 0; c < 3; c++) {
                        double v = linear_sample(src + channels[c] * component, *depth);
                        if (premultiplied) v = MIN(v / alpha, 1.0);
                        double linear = linear_rgb ? v : gray_icc && r == g ?
                            pow(v, 2.2) : v <= .04045 ? v / 12.92 : pow((v + .055) / 1.055, 2.4);
                        sum[c] += weight * linear;
                    }
                    sum[3] += weight;
                }
            }
            float *dst = out + 4 * ((gsize)y * *width + x);
            for (int c = 0; c < 3; c++) dst[c] = sum[3] > 0 ? sum[c] / sum[3] : 0;
            dst[3] = CLAMP(sum[3] / ((sx1 - sx0) * (sy1 - sy0)), 0, 1);
        }
    }
    return g_bytes_new_take(out, count * sizeof(float));
}

typedef struct {
    GlyFrame *frame;
    int edge, width, height, depth;
    double left, top, region_width, region_height;
    gboolean nearest, gray_icc, linear_rgb;
} PixelJob;

static void pixel_job_free(PixelJob *job)
{
    g_object_unref(job->frame);
    g_free(job);
}

static void convert_thread(GTask *task, gpointer source, gpointer data, GCancellable *stop)
{
    PixelJob *job = data;
    GError *error = NULL;
    GBytes *pixels = convert_pixels(job->frame, job->edge, job->left, job->top,
        job->region_width, job->region_height, job->nearest, job->gray_icc, job->linear_rgb,
        &job->width, &job->height, &job->depth, stop, &error);
    if (pixels) g_task_return_pointer(task, pixels, (GDestroyNotify)g_bytes_unref);
    else g_task_return_error(task, error);
}

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

void image_glycin_pixels_async(GlyFrame *frame, int edge, double left, double top,
                                      double region_width, double region_height, gboolean nearest,
                                      gboolean gray_icc, GCancellable *stop,
                                      GAsyncReadyCallback callback, gpointer data)
{
    image_glycin_pixels_profile_async(frame, edge, left, top, region_width,
        region_height, nearest, gray_icc, FALSE, stop, callback, data);
}

GBytes *image_glycin_pixels_finish(GAsyncResult *result, int *width, int *height,
                                           int *depth, GError **error)
{
    PixelJob *job = g_task_get_task_data(G_TASK(result));
    *width = job->width;
    *height = job->height;
    *depth = job->depth;
    return g_task_propagate_pointer(G_TASK(result), error);
}
