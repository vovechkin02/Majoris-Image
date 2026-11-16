#include <math.h>
#include <string.h>
#include <sys/resource.h>

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

guint32 __wrap_gly_frame_get_width(GlyFrame *frame)
{
    return frame_data(frame)->width;
}

guint32 __wrap_gly_frame_get_height(GlyFrame *frame)
{
    return frame_data(frame)->height;
}

guint32 __wrap_gly_frame_get_stride(GlyFrame *frame)
{
    return frame_data(frame)->stride;
}

GlyMemoryFormat __wrap_gly_frame_get_memory_format(GlyFrame *frame)
{
    return frame_data(frame)->layout->format;
}

GBytes *__wrap_gly_frame_get_buf_bytes(GlyFrame *frame)
{
    return frame_data(frame)->bytes;
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
        const unsigned alpha[] = {0, 1, 128, 254, 255};
        return layout->channels[3] < 0 ? max : alpha[(x + y) % 5] * (max / 255);
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

static void test_layouts(void)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GObject) frame = g_object_new(G_TYPE_OBJECT, NULL);
    FrameData data;
    g_object_set_data(frame, "frame-data", &data);
    for (guint i = 0; i < G_N_ELEMENTS(layouts); i++) {
        const Layout *layout = &layouts[i];
        make_frame(&data, layout, 257, 7);
        for (int mode = 0; mode < 3; mode++) {
            Result result = {.loop = loop};
            start(frame, &result, 257, FALSE, FALSE, mode == 1, mode == 2, NULL);
            wait_for_conversion(&result);
            g_assert_no_error(result.error);
            g_assert_cmpint(result.width, ==, 257);
            g_assert_cmpint(result.height, ==, 7);
            g_assert_cmpint(result.depth, ==, layout->depth);
            g_assert_cmpuint(g_bytes_get_size(result.bytes), ==, 257 * 7 * 4 * sizeof(float));
            const float *actual = g_bytes_get_data(result.bytes, NULL);
            for (guint y = 0; y < 7; y++) {
                for (guint x = 0; x < 257; x++) {
                    for (int c = 0; c < 4; c++) {
                        float expected = expected_sample(layout, x, y, c, mode == 1, mode == 2);
                        g_assert_cmpfloat(actual[4 * (y * 257 + x) + c], ==, expected);
                    }
                }
            }
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
        {"rgb16", 9, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
        {"rgba16", 10, 1601, 1203, 701, FALSE, FALSE, FALSE, FALSE},
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
    int repetitions = index == 0 ? 101 : 6;
    for (int i = 0; i < repetitions; i++) {
        struct rusage before, after;
        Result first = {.loop = loop}, second = {.loop = loop};
        getrusage(RUSAGE_SELF, &before);
        gint64 started = g_get_monotonic_time();
        start(frame, &first, cases[index].edge, cases[index].crop, FALSE, cases[index].gray,
              cases[index].linear, NULL);
        if (cases[index].concurrent) {
            start(frame, &second, cases[index].edge, cases[index].crop, FALSE, cases[index].gray,
                  cases[index].linear, NULL);
            wait_for_conversion(&second);
        }
        wait_for_conversion(&first);
        gint64 elapsed = g_get_monotonic_time() - started;
        getrusage(RUSAGE_SELF, &after);
        g_assert_no_error(first.error);
        if (cases[index].concurrent) {
            g_assert_no_error(second.error);
            g_assert_true(g_bytes_equal(first.bytes, second.bytes));
            g_bytes_unref(second.bytes);
        }
        g_autofree char *hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, first.bytes);
        guint width = cases[index].crop ? floor(.71 * data.width + 1e-7) : data.width;
        guint height = cases[index].crop ? floor(.81 * data.height + 1e-7) : data.height;
        double throughput = (double)width * height * (cases[index].concurrent ? 2 : 1) *
                            G_USEC_PER_SEC / MAX(elapsed, 1);
        g_print("%s,%s,%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT ",%ld,%dx%d,%.0f,%s\n", name,
                i == 0 ? "cold" : "warm", elapsed, cpu_time(&after) - cpu_time(&before),
                after.ru_maxrss, first.width, first.height, throughput, hash);
        g_bytes_unref(first.bytes);
    }
    g_bytes_unref(data.bytes);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--benchmark") == 0) {
        benchmark(argv[2]);
        return 0;
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/glycin/concurrent-cropped-resampling", test_concurrent_crop);
    g_test_add_func("/glycin/layouts-and-transfer", test_layouts);
    g_test_add_func("/glycin/resampling-and-alpha", test_resampling);
    g_test_add_func("/glycin/errors-and-cancellation", test_errors);
    return g_test_run();
}
