#include <math.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include <glycin.h>

void image_glycin_pixels_profile_async(GlyFrame *frame, int edge, double left, double top,
                                       double region_width, double region_height, gboolean nearest,
                                       gboolean gray_icc, gboolean linear_rgb, GCancellable *stop,
                                       GAsyncReadyCallback callback, gpointer data);
GBytes *image_glycin_pixels_finish(GAsyncResult *result, int *width, int *height, int *depth,
                                   GError **error);

typedef struct {
    GlyMemoryFormat format;
    int depth;
    int bands;
    int channels[4];
} Layout;

typedef struct {
    GBytes *bytes;
    guint width, height, stride;
    const Layout *layout;
} FrameData;

typedef struct {
    GMainLoop *loop;
    GBytes *bytes;
    GError *error;
    int width, height, depth;
    gboolean done;
} Result;

static const Layout layouts[] = {
    {GLY_MEMORY_R8G8B8, 8, 3, {0, 1, 2, -1}},
    {GLY_MEMORY_B8G8R8, 8, 3, {2, 1, 0, -1}},
    {GLY_MEMORY_R8G8B8A8, 8, 4, {0, 1, 2, 3}},
    {GLY_MEMORY_B8G8R8A8, 8, 4, {2, 1, 0, 3}},
    {GLY_MEMORY_A8R8G8B8, 8, 4, {1, 2, 3, 0}},
    {GLY_MEMORY_A8B8G8R8, 8, 4, {3, 2, 1, 0}},
    {GLY_MEMORY_R8G8B8A8_PREMULTIPLIED, 8, 4, {0, 1, 2, 3}},
    {GLY_MEMORY_B8G8R8A8_PREMULTIPLIED, 8, 4, {2, 1, 0, 3}},
    {GLY_MEMORY_A8R8G8B8_PREMULTIPLIED, 8, 4, {1, 2, 3, 0}},
    {GLY_MEMORY_R16G16B16, 16, 3, {0, 1, 2, -1}},
    {GLY_MEMORY_R16G16B16A16, 16, 4, {0, 1, 2, 3}},
    {GLY_MEMORY_R16G16B16A16_PREMULTIPLIED, 16, 4, {0, 1, 2, 3}},
    {GLY_MEMORY_G8, 8, 1, {0, 0, 0, -1}},
    {GLY_MEMORY_G8A8, 8, 2, {0, 0, 0, 1}},
    {GLY_MEMORY_G8A8_PREMULTIPLIED, 8, 2, {0, 0, 0, 1}},
    {GLY_MEMORY_G16, 16, 1, {0, 0, 0, -1}},
    {GLY_MEMORY_G16A16, 16, 2, {0, 0, 0, 1}},
    {GLY_MEMORY_G16A16_PREMULTIPLIED, 16, 2, {0, 0, 0, 1}},
};

// Glycin 1 has no public frame constructor. Substitute only its frame accessors
// so malformed extents and every supported layout reach the production GTask.
static FrameData *frame_data(GlyFrame *frame)
{
    return g_object_get_data(G_OBJECT(frame), "frame-data");
}

guint32 __real_gly_frame_get_width(GlyFrame *frame);
guint32 __real_gly_frame_get_height(GlyFrame *frame);
guint32 __real_gly_frame_get_stride(GlyFrame *frame);
GlyMemoryFormat __real_gly_frame_get_memory_format(GlyFrame *frame);
GBytes *__real_gly_frame_get_buf_bytes(GlyFrame *frame);

guint32 __wrap_gly_frame_get_width(GlyFrame *frame)
{
    FrameData *data = frame_data(frame);
    return data ? data->width : __real_gly_frame_get_width(frame);
}

guint32 __wrap_gly_frame_get_height(GlyFrame *frame)
{
    FrameData *data = frame_data(frame);
    return data ? data->height : __real_gly_frame_get_height(frame);
}

guint32 __wrap_gly_frame_get_stride(GlyFrame *frame)
{
    FrameData *data = frame_data(frame);
    return data ? data->stride : __real_gly_frame_get_stride(frame);
}

GlyMemoryFormat __wrap_gly_frame_get_memory_format(GlyFrame *frame)
{
    FrameData *data = frame_data(frame);
    return data ? data->layout->format : __real_gly_frame_get_memory_format(frame);
}

GBytes *__wrap_gly_frame_get_buf_bytes(GlyFrame *frame)
{
    FrameData *data = frame_data(frame);
    return data ? data->bytes : __real_gly_frame_get_buf_bytes(frame);
}

static GCancellable *cancel_during_conversion;
static unsigned cancellation_checks;
gboolean __real_g_cancellable_set_error_if_cancelled(GCancellable *stop, GError **error);

gboolean __wrap_g_cancellable_set_error_if_cancelled(GCancellable *stop, GError **error)
{
    if (stop != NULL && stop == cancel_during_conversion && ++cancellation_checks == 5) {
        g_cancellable_cancel(stop);
    }
    return __real_g_cancellable_set_error_if_cancelled(stop, error);
}

static gint fail_output_allocation;
gpointer __real_g_try_malloc_n(gsize blocks, gsize size);

gpointer __wrap_g_try_malloc_n(gsize blocks, gsize size)
{
    if (g_atomic_int_compare_and_exchange(&fail_output_allocation, 1, 0)) {
        return NULL;
    }
    return __real_g_try_malloc_n(blocks, size);
}

static unsigned sample(const Layout *layout, unsigned x, unsigned y, int channel)
{
    unsigned max = layout->depth == 8 ? 255 : 65535;
    if (channel == 3) {
        const unsigned alpha[] = {0, 1, (max + 1) / 2, max - 1, max};
        return layout->channels[3] < 0 ? max : alpha[(x + y) % G_N_ELEMENTS(alpha)];
    }
    if (layout->bands <= 2) {
        channel = 0;
    }
    return (x * 13 + y * 157 + channel * 83) % (max + 1);
}

static void make_frame(FrameData *data, const Layout *layout, guint width, guint height)
{
    int component = layout->depth / 8;
    guint stride = width * layout->bands * component + 7;
    guint8 *pixels = g_malloc0((gsize)stride * height);
    for (guint y = 0; y < height; y++) {
        for (guint x = 0; x < width; x++) {
            for (int c = 0; c < 4; c++) {
                if (layout->channels[c] < 0) {
                    continue;
                }
                guint16 value = sample(layout, x, y, c);
                guint8 *dst = pixels + (gsize)y * stride +
                              (x * layout->bands + layout->channels[c]) * component;
                if (component == 1) {
                    *dst = value;
                } else {
                    memcpy(dst, &value, sizeof value);
                }
            }
        }
    }
    *data = (FrameData){g_bytes_new_take(pixels, (gsize)stride * height), width, height, stride,
                        layout};
}

static void complete(GObject *source G_GNUC_UNUSED, GAsyncResult *async_result, gpointer user_data)
{
    Result *result = user_data;
    result->bytes = image_glycin_pixels_finish(async_result, &result->width, &result->height,
                                               &result->depth, &result->error);
    result->done = TRUE;
    g_main_loop_quit(result->loop);
}

static void wait_for_conversion(Result *result)
{
    while (!result->done) {
        g_main_loop_run(result->loop);
    }
}

static void start(GObject *frame, Result *result, int edge, gboolean crop, gboolean nearest,
                  gboolean gray, gboolean linear, GCancellable *stop)
{
    image_glycin_pixels_profile_async((GlyFrame *)frame, edge, crop ? .13 : 0, crop ? .07 : 0,
                                      crop ? .71 : 1, crop ? .81 : 1, nearest, gray, linear, stop,
                                      complete, result);
}

static float expected_sample(const Layout *layout, guint x, guint y, int c, gboolean gray,
                             gboolean linear)
{
    double max = layout->depth == 8 ? 255.0 : 65535.0;
    double alpha = sample(layout, x, y, 3) / max;
    if (c == 3) {
        return alpha;
    }
    if (alpha == 0) {
        return 0;
    }
    double value = sample(layout, x, y, c) / max;
    if (gly_memory_format_is_premultiplied(layout->format)) {
        value = MIN(1.0, value / alpha);
    }
    if (!linear) {
        if (gray && layout->bands <= 2) {
            value = pow(value, 2.2);
        } else if (value <= .04045) {
            value /= 12.92;
        } else {
            value = pow((value + .055) / 1.055, 2.4);
        }
    }
    return alpha * value / alpha;
}

static void check_pixels(const Result *result, const FrameData *data, gboolean gray,
                         gboolean linear)
{
    g_assert_no_error(result->error);
    g_assert_cmpint(result->width, ==, (int)data->width);
    g_assert_cmpint(result->height, ==, (int)data->height);
    g_assert_cmpint(result->depth, ==, data->layout->depth);
    g_assert_cmpuint(g_bytes_get_size(result->bytes), ==,
                     (gsize)data->width * data->height * 4 * sizeof(float));
    const float *actual = g_bytes_get_data(result->bytes, NULL);
    for (guint y = 0; y < data->height; y++) {
        for (guint x = 0; x < data->width; x++) {
            for (int c = 0; c < 4; c++) {
                float expected = expected_sample(data->layout, x, y, c, gray, linear);
                g_assert_cmpfloat(actual[4 * (y * data->width + x) + c], ==, expected);
            }
        }
    }
}

static void test_concurrent_16bit(void)
{
    const guint indices[] = {9, 9, 15, 0, 9};
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GCancellable) stop = g_cancellable_new();
    FrameData data[G_N_ELEMENTS(indices)];
    GObject *frames[G_N_ELEMENTS(indices)];
    Result results[G_N_ELEMENTS(indices)] = {0};
    cancel_during_conversion = stop;
    cancellation_checks = 0;
    for (guint i = 0; i < G_N_ELEMENTS(indices); i++) {
        make_frame(&data[i], &layouts[indices[i]], 257, 129);
        frames[i] = g_object_new(G_TYPE_OBJECT, NULL);
        g_object_set_data(frames[i], "frame-data", &data[i]);
        results[i].loop = loop;
    }
    for (guint i = 0; i < G_N_ELEMENTS(indices); i++) {
        start(frames[i], &results[i], 257, FALSE, FALSE, indices[i] == 15, FALSE,
              i == 4 ? stop : NULL);
    }
    for (guint i = 0; i < G_N_ELEMENTS(indices); i++) {
        wait_for_conversion(&results[i]);
        if (i == 4) {
            g_assert_error(results[i].error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
            g_assert_null(results[i].bytes);
            g_clear_error(&results[i].error);
        } else {
            check_pixels(&results[i], &data[i], indices[i] == 15, FALSE);
            g_bytes_unref(results[i].bytes);
        }
        g_object_unref(frames[i]);
        g_bytes_unref(data[i].bytes);
    }
    cancel_during_conversion = NULL;
}

static void test_16bit_sample_range(void)
{
    enum { EDGE = 256, SAMPLES = EDGE * EDGE };
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    guint16 *samples = g_new(guint16, SAMPLES);
    for (guint i = 0; i < SAMPLES; i++) {
        samples[i] = i;
    }
    g_autoptr(GBytes) bytes = g_bytes_new_take(samples, SAMPLES * sizeof(*samples));
    FrameData data = {bytes, EDGE, EDGE, EDGE * sizeof(*samples), &layouts[15]};
    g_object_set_data(frame, "frame-data", &data);
    for (int gray = 0; gray < 2; gray++) {
        Result result = {.loop = loop};
        start(frame, &result, EDGE, FALSE, FALSE, gray, FALSE, NULL);
        wait_for_conversion(&result);
        g_assert_no_error(result.error);
        g_assert_cmpint(result.width, ==, EDGE);
        g_assert_cmpint(result.height, ==, EDGE);
        g_assert_cmpint(result.depth, ==, 16);
        g_assert_cmpuint(g_bytes_get_size(result.bytes), ==, SAMPLES * 4 * sizeof(float));
        const float *pixels = g_bytes_get_data(result.bytes, NULL);
        // Includes 0, 1, the sRGB breakpoint (2650/2651), and both endpoints.
        for (guint i = 0; i < SAMPLES; i++) {
            double value = i / 65535.0;
            float expected =
                gray ? pow(value, 2.2)
                     : (value <= .04045 ? value / 12.92 : pow((value + .055) / 1.055, 2.4));
            for (int c = 0; c < 3; c++) {
                g_assert_cmpfloat(pixels[4 * i + c], ==, expected);
            }
            g_assert_cmpfloat(pixels[4 * i + 3], ==, 1);
        }
        g_bytes_unref(result.bytes);
    }
}

static void test_layouts(void)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    FrameData data;
    g_object_set_data(frame, "frame-data", &data);
    for (guint i = 0; i < G_N_ELEMENTS(layouts); i++) {
        const Layout *layout = &layouts[i];
        guint height = layout->depth == 16 ? 129 : 7;
        make_frame(&data, layout, 257, height);
        for (int mode = 0; mode < 3; mode++) {
            Result result = {.loop = loop};
            start(frame, &result, 257, FALSE, FALSE, mode == 1, mode == 2, NULL);
            wait_for_conversion(&result);
            check_pixels(&result, &data, mode == 1, mode == 2);
            g_bytes_unref(result.bytes);
        }
        g_bytes_unref(data.bytes);
    }
}

static void test_resampling(void)
{
    const guint8 input[] = {255, 30, 20, 255, 0, 255, 0, 0, 5, 10, 250, 255};
    FrameData data = {g_bytes_new_static(input, sizeof input), 3, 1, sizeof input, &layouts[2]};
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    g_object_set_data(frame, "frame-data", &data);
    Result area = {.loop = loop};
    Result nearest = {.loop = loop};
    start(frame, &area, 2, FALSE, FALSE, FALSE, TRUE, NULL);
    start(frame, &nearest, 2, FALSE, TRUE, FALSE, TRUE, NULL);
    wait_for_conversion(&area);
    wait_for_conversion(&nearest);
    g_assert_no_error(area.error);
    g_assert_no_error(nearest.error);
    g_assert_cmpint(area.width, ==, 2);
    g_assert_cmpint(area.height, ==, 1);
    const float area_expected[] = {1,         30.f / 255, 20.f / 255,  2.f / 3,
                                   5.f / 255, 10.f / 255, 250.f / 255, 2.f / 3};
    const float nearest_expected[] = {1, 30.f / 255, 20.f / 255, 1, 0, 0, 0, 0};
    g_assert_cmpmem(g_bytes_get_data(area.bytes, NULL), g_bytes_get_size(area.bytes), area_expected,
                    sizeof area_expected);
    g_assert_cmpmem(g_bytes_get_data(nearest.bytes, NULL), g_bytes_get_size(nearest.bytes),
                    nearest_expected, sizeof nearest_expected);
    g_bytes_unref(area.bytes);
    g_bytes_unref(nearest.bytes);
    g_bytes_unref(data.bytes);
}

static void test_concurrent_crop(void)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    FrameData data;
    make_frame(&data, &layouts[0], 157, 93);
    g_object_set_data(frame, "frame-data", &data);
    Result first = {.loop = loop}, second = {.loop = loop}, repeat = {.loop = loop};
    start(frame, &first, 17, TRUE, FALSE, FALSE, FALSE, NULL);
    start(frame, &second, 17, TRUE, FALSE, FALSE, FALSE, NULL);
    wait_for_conversion(&first);
    wait_for_conversion(&second);
    start(frame, &repeat, 17, TRUE, FALSE, FALSE, FALSE, NULL);
    wait_for_conversion(&repeat);
    g_assert_no_error(first.error);
    g_assert_no_error(second.error);
    g_assert_no_error(repeat.error);
    g_assert_cmpint(first.width, ==, 17);
    g_assert_cmpint(first.height, ==, 11);
    g_assert_true(g_bytes_equal(first.bytes, second.bytes));
    g_assert_true(g_bytes_equal(first.bytes, repeat.bytes));

    Result unscaled = {.loop = loop};
    start(frame, &unscaled, 157, TRUE, TRUE, FALSE, FALSE, NULL);
    wait_for_conversion(&unscaled);
    g_assert_no_error(unscaled.error);
    g_assert_cmpint(unscaled.width, ==, 111);
    g_assert_cmpint(unscaled.height, ==, 75);
    const float *pixels = g_bytes_get_data(unscaled.bytes, NULL);
    for (int y = 0; y < unscaled.height; y++) {
        for (int x = 0; x < unscaled.width; x++) {
            for (int c = 0; c < 4; c++) {
                float expected = expected_sample(&layouts[0], 20 + x, 6 + y, c, FALSE, FALSE);
                g_assert_cmpfloat(pixels[4 * (y * unscaled.width + x) + c], ==, expected);
            }
        }
    }
    g_bytes_unref(unscaled.bytes);
    g_bytes_unref(repeat.bytes);
    g_bytes_unref(second.bytes);
    g_bytes_unref(first.bytes);
    g_bytes_unref(data.bytes);
}

static void expect_error(GObject *frame, GMainLoop *loop, int edge, GCancellable *stop, int code)
{
    Result result = {.loop = loop};
    start(frame, &result, edge, FALSE, FALSE, FALSE, FALSE, stop);
    wait_for_conversion(&result);
    g_assert_error(result.error, G_IO_ERROR, code);
    g_assert_null(result.bytes);
    g_clear_error(&result.error);
}

static void test_errors(void)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    g_autoptr(GCancellable) stop = g_cancellable_new();
    FrameData data;
    g_object_set_data(frame, "frame-data", &data);
    make_frame(&data, &layouts[0], 257, 33);
    Layout unsupported = {.format = GLY_MEMORY_R16G16B16_FLOAT};
    FrameData valid = data;
    data.layout = &unsupported;
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_NOT_SUPPORTED);
    data = valid;
    data.width = 0;
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_INVALID_DATA);
    data.width = 67108865;
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_INVALID_DATA);
    data = valid;
    data.stride = 1;
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_INVALID_DATA);
    data = valid;
    data.bytes = g_bytes_new_static("", 1);
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_INVALID_DATA);
    g_bytes_unref(data.bytes);
    data = valid;
    expect_error(frame, loop, 4097, NULL, G_IO_ERROR_INVALID_DATA);
    g_atomic_int_set(&fail_output_allocation, 1);
    expect_error(frame, loop, 32, NULL, G_IO_ERROR_NO_SPACE);
    cancel_during_conversion = stop;
    cancellation_checks = 0;
    expect_error(frame, loop, 32, stop, G_IO_ERROR_CANCELLED);
    g_assert_cmpuint(cancellation_checks, ==, 5);
    cancel_during_conversion = NULL;
    g_bytes_unref(data.bytes);
}

static gint64 cpu_time(const struct rusage *usage)
{
    return (usage->ru_utime.tv_sec + usage->ru_stime.tv_sec) * G_USEC_PER_SEC +
           usage->ru_utime.tv_usec + usage->ru_stime.tv_usec;
}

static long resident_kib(void)
{
    g_autofree char *statm = NULL;
    if (!g_file_get_contents("/proc/self/statm", &statm, NULL, NULL)) {
        return -1;
    }
    const char *resident = strchr(statm, ' ');
    return resident ? g_ascii_strtoll(resident, NULL, 10) * sysconf(_SC_PAGESIZE) / 1024 : -1;
}

static void benchmark_real_frame(const char *path, gboolean gray)
{
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    for (int i = 0; i < 6; i++) {
        g_autoptr(GError) error = NULL;
        struct rusage before, after;
        long rss = resident_kib();
        getrusage(RUSAGE_SELF, &before);
        gint64 started = g_get_monotonic_time();
        g_autoptr(GlyLoader) loader = gly_loader_new(file);
        gly_loader_set_sandbox_selector(loader, GLY_SANDBOX_SELECTOR_BWRAP);
        g_autoptr(GlyImage) image = gly_loader_load(loader, &error);
        g_assert_no_error(error);
        g_autoptr(GlyFrame) frame = gly_image_next_frame(image, &error);
        g_assert_no_error(error);
        gint64 loaded = g_get_monotonic_time();
        GlyMemoryFormat format = gly_frame_get_memory_format(frame);
        g_autoptr(GEnumClass) formats = g_type_class_ref(gly_memory_format_get_type());
        Result result = {.loop = loop};
        start(G_OBJECT(frame), &result, 701, FALSE, FALSE, gray, FALSE, NULL);
        wait_for_conversion(&result);
        gint64 converted = g_get_monotonic_time();
        getrusage(RUSAGE_SELF, &after);
        g_assert_no_error(result.error);
        g_autofree char *hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, result.bytes);
        g_print("%s,%s,%ux%u,%s,%d,%dx%d,%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT
                ",%" G_GINT64_FORMAT ",%ld,%ld,%ld,%s\n",
                i == 0 ? "cold" : "warm", g_enum_get_value(formats, format)->value_name,
                gly_frame_get_width(frame), gly_frame_get_height(frame),
                gly_memory_format_is_premultiplied(format) ? "premultiplied" : "straight",
                result.depth, result.width, result.height, loaded - started, converted - loaded,
                cpu_time(&after) - cpu_time(&before), rss, resident_kib(), after.ru_maxrss, hash);
        g_bytes_unref(result.bytes);
    }
}

static void benchmark(const char *name)
{
    static const struct {
        const char *name;
        int layout, width, height, edge;
        gboolean crop, gray, linear, concurrent;
    } cases[] = {
        {"small-rgb8", 0, 31, 19, 31, FALSE, FALSE, FALSE, FALSE},
        {"large-rgb8", 0, 1601, 1203, 1601, FALSE, FALSE, FALSE, FALSE},
        {"rgba8", 2, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"premultiplied-rgba8", 6, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"gray8", 12, 1601, 1203, 701, FALSE, TRUE, FALSE, FALSE},
        {"small-rgb16", 9, 31, 19, 31, FALSE, FALSE, FALSE, FALSE},
        {"threshold-rgb16", 9, 256, 128, 16, FALSE, FALSE, FALSE, FALSE},
        {"rgb16", 9, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"rgba16", 10, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"gray16", 15, 1601, 1203, 701, FALSE, TRUE, FALSE, FALSE},
        {"gray-alpha16", 16, 1601, 1203, 701, FALSE, TRUE, FALSE, FALSE},
        {"premultiplied-rgba16", 11, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"premultiplied-gray-alpha16", 17, 1601, 1203, 701, FALSE, TRUE, FALSE, FALSE},
        {"linear-rgb16", 9, 1601, 1203, 701, FALSE, FALSE, TRUE, FALSE},
        {"fractional-rgb16", 9, 1601, 1203, 113, TRUE, FALSE, FALSE, FALSE},
        {"concurrent-rgb16", 9, 1601, 1203, 701, FALSE, FALSE, FALSE, TRUE},
        {"concurrent-rgb16-gray16", 9, 1601, 1203, 701, FALSE, FALSE, FALSE, TRUE},
        {"linear-rgb", 0, 1601, 1203, 701, FALSE, FALSE, TRUE, FALSE},
        {"large-downscale", 0, 2501, 1703, 113, FALSE, FALSE, FALSE, FALSE},
        {"fractional-crop", 2, 1601, 1203, 713, TRUE, FALSE, FALSE, FALSE},
        {"concurrent", 0, 1601, 1203, 701, FALSE, FALSE, FALSE, TRUE},
    };
    guint index = 0;
    while (index < G_N_ELEMENTS(cases) && strcmp(name, cases[index].name) != 0) {
        index++;
    }
    g_assert_cmpuint(index, <, G_N_ELEMENTS(cases));
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    FrameData data;
    make_frame(&data, &layouts[cases[index].layout], cases[index].width, cases[index].height);
    g_object_set_data(frame, "frame-data", &data);
    gboolean mixed = strcmp(name, "concurrent-rgb16-gray16") == 0;
    g_autoptr(GObject) gray_frame = NULL;
    FrameData gray_data = {0};
    if (mixed) {
        gray_frame = g_object_new(G_TYPE_OBJECT, NULL);
        make_frame(&gray_data, &layouts[15], data.width, data.height);
        g_object_set_data(gray_frame, "frame-data", &gray_data);
    }
    int repetitions = cases[index].width == 31 ? 101 : 6;
    for (int i = 0; i < repetitions; i++) {
        struct rusage before, after;
        Result first = {.loop = loop}, second = {.loop = loop};
        long rss = resident_kib();
        getrusage(RUSAGE_SELF, &before);
        gint64 started = g_get_monotonic_time();
        start(frame, &first, cases[index].edge, cases[index].crop, FALSE, cases[index].gray,
              cases[index].linear, NULL);
        if (cases[index].concurrent) {
            start(mixed ? gray_frame : frame, &second, cases[index].edge, cases[index].crop, FALSE,
                  mixed || cases[index].gray, cases[index].linear, NULL);
            wait_for_conversion(&second);
        }
        wait_for_conversion(&first);
        gint64 elapsed = g_get_monotonic_time() - started;
        getrusage(RUSAGE_SELF, &after);
        g_assert_no_error(first.error);
        g_autofree char *second_hash = NULL;
        if (cases[index].concurrent) {
            g_assert_no_error(second.error);
            if (mixed) {
                second_hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, second.bytes);
            } else {
                g_assert_true(g_bytes_equal(first.bytes, second.bytes));
            }
            g_bytes_unref(second.bytes);
        }
        g_autofree char *hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, first.bytes);
        guint width = cases[index].crop ? floor(.71 * data.width + 1e-7) : data.width;
        guint height = cases[index].crop ? floor(.81 * data.height + 1e-7) : data.height;
        double throughput = (double)width * height * (cases[index].concurrent ? 2 : 1) *
                            G_USEC_PER_SEC / MAX(elapsed, 1);
        g_print("%s,%s,%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT ",%ld,%dx%d,%.0f,%s,%ld,%ld,%s\n",
                name, i == 0 ? "cold" : "warm", elapsed, cpu_time(&after) - cpu_time(&before),
                after.ru_maxrss, first.width, first.height, throughput, hash, rss, resident_kib(),
                second_hash ? second_hash : "");
        g_bytes_unref(first.bytes);
    }
    g_clear_pointer(&gray_data.bytes, g_bytes_unref);
    g_bytes_unref(data.bytes);
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--real-frame") == 0) {
        benchmark_real_frame(argv[2], strcmp(argv[3], "gray-icc") == 0);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--benchmark") == 0) {
        benchmark(argv[2]);
        return 0;
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/glycin/concurrent-16bit-transfer", test_concurrent_16bit);
    g_test_add_func("/glycin/complete-16bit-sample-range", test_16bit_sample_range);
    g_test_add_func("/glycin/concurrent-cropped-resampling", test_concurrent_crop);
    g_test_add_func("/glycin/layouts-and-transfer", test_layouts);
    g_test_add_func("/glycin/resampling-and-alpha", test_resampling);
    g_test_add_func("/glycin/errors-and-cancellation", test_errors);
    return g_test_run();
}
