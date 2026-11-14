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

static GBytes *convert_pixels(GlyFrame *frame, int edge, int *width, int *height,
                                    int *depth, GCancellable *stop, GError **error)
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
                            "Glycin colour transfer or pixel layout is unavailable");
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

    double scale = MIN(1.0, (double)edge / MAX(w, h));
    *width = MAX(1, (int)floor(w * scale));
    *height = MAX(1, (int)floor(h * scale));
    gsize count = (gsize)*width * *height * 4;
    float *out = g_try_new(float, count);
    if (!out) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Glycin preview memory limit");
        return NULL;
    }
    gboolean premultiplied = gly_memory_format_is_premultiplied(format);
    int channels[] = {r, g, b};
    for (int y = 0; y < *height; y++) {
        if (g_cancellable_set_error_if_cancelled(stop, error)) { g_free(out); return NULL; }
        for (int x = 0; x < *width; x++) {
            guint sx = (guint)((guint64)x * w / *width);
            guint sy = (guint)((guint64)y * h / *height);
            const guint8 *src = input + (gsize)sy * stride + (gsize)sx * pixel;
            float *dst = out + 4 * ((gsize)y * *width + x);
            double alpha = a < 0 ? 1 : linear_sample(src + a * component, *depth);
            for (int c = 0; c < 3; c++) {
                double v = linear_sample(src + channels[c] * component, *depth);
                if (premultiplied) v = alpha > 0 ? MIN(v / alpha, 1.0) : 0;
                dst[c] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
            }
            dst[3] = alpha;
        }
    }
    return g_bytes_new_take(out, count * sizeof(float));
}

typedef struct {
    GlyFrame *frame;
    int edge, width, height, depth;
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
    GBytes *pixels = convert_pixels(job->frame, job->edge, &job->width, &job->height, &job->depth, stop, &error);
    if (pixels) g_task_return_pointer(task, pixels, (GDestroyNotify)g_bytes_unref);
    else g_task_return_error(task, error);
}

void majoris_image_glycin_pixels_async(GlyFrame *frame, int edge, GCancellable *stop,
                                       GAsyncReadyCallback callback, gpointer data)
{
    PixelJob *job = g_new0(PixelJob, 1);
    job->frame = g_object_ref(frame);
    job->edge = edge;
    GTask *task = g_task_new(NULL, stop, callback, data);
    g_task_set_task_data(task, job, (GDestroyNotify)pixel_job_free);
    g_task_run_in_thread(task, convert_thread);
    g_object_unref(task);
}

GBytes *majoris_image_glycin_pixels_finish(GAsyncResult *result, int *width, int *height,
                                           int *depth, GError **error)
{
    PixelJob *job = g_task_get_task_data(G_TASK(result));
    *width = job->width;
    *height = job->height;
    *depth = job->depth;
    return g_task_propagate_pointer(G_TASK(result), error);
}
