/* libFuzzer and deterministic smoke runner for production entry points. */
#include <stdint.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "bridge.h"

gboolean image_thumbnail_valid(GBytes *bytes, int edge);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 512 * 1024) {
        return 0;
    }
    unsigned parser = data[0] % 5;
    data++;
    size--;
    if (parser == 0) {
        g_autoptr(GBytes) bytes = g_bytes_new_static(data, size);
        image_thumbnail_valid(bytes, 1024);
    } else if (parser == 1) {
        const guint8 pixels[] = {0, 127, 255};
        VipsImage *image =
            vips_image_new_from_memory_copy(pixels, sizeof pixels, 1, 1, 3, VIPS_FORMAT_UCHAR);
        image->Type = VIPS_INTERPRETATION_sRGB;
        vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, data, size);
        g_autofree char *metadata = image_codec_image_details(image);
        g_object_unref(image);
    } else {
        g_autofree char *path = NULL;
        g_autoptr(GError) error = NULL;
        int fd = g_file_open_tmp("imagekit-fuzz-XXXXXX", &path, &error);
        if (fd < 0) {
            g_error("Cannot create fuzz input: %s", error->message);
        }
        close(fd);
        if (!g_file_set_contents(path, (const char *)data, size, &error)) {
            g_error("Cannot write fuzz input: %s", error->message);
        }
        VipsImage *image = parser == 4 ? image_codec_read_pixels(path, &error)
                                       : image_codec_sequence(path, 1, parser == 3, 0, &error);
        if (image) {
            g_assert_cmpint(image->Xsize, >, 0);
            g_assert_cmpint(image->Ysize, >, 0);
            g_object_unref(image);
        }
        if (g_unlink(path)) {
            g_error("Cannot remove fuzz input");
        }
    }
    vips_error_clear();
    return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    if (VIPS_INIT((*argv)[0])) {
        return 1;
    }
    vips_cache_set_max(0);
    vips_concurrency_set(2);
    return 0;
}

#ifndef IMAGEKIT_LIBFUZZER
static void mutate_fixture(const char *path, guint8 selector, GRand *random)
{
    g_autofree char *data = NULL;
    gsize size = 0;
    g_autoptr(GError) error = NULL;
    if (!g_file_get_contents(path, &data, &size, &error)) {
        g_error("%s", error->message);
    }
    g_assert_cmpuint(size, >, 0);
    g_assert_cmpuint(size, <, 512 * 1024);
    g_autofree guint8 *input = g_malloc(size + 1);
    input[0] = selector;
    for (unsigned trial = 0; trial < 48; trial++) {
        memcpy(input + 1, data, size);
        gsize length = size + 1;
        if (trial % 2) {
            input[1 + g_rand_int_range(random, 0, size)] ^= 1 << (trial % 8);
        } else if (trial) {
            length = g_rand_int_range(random, 2, size + 1);
        }
        if (g_getenv("IMAGEKIT_TEST_TRACE")) {
            g_printerr("%s parser %u trial %u length %zu\n", path, selector, trial, length);
        }
        LLVMFuzzerTestOneInput(input, length);
    }
}

int main(int argc, char **argv)
{
    if (argc != 4 || LLVMFuzzerInitialize(&argc, &argv)) {
        return 2;
    }
    guint32 seed = 0x8f64ac91;
    g_autoptr(GRand) random = g_rand_new_with_seed(seed);
    g_printerr("Native parser smoke seed %#x\n", seed);
    mutate_fixture(argv[1], 0, random);
    mutate_fixture(argv[1], 1, random);
    mutate_fixture(argv[2], 2, random);
    mutate_fixture(argv[3], 3, random);
    const guint8 pixels[] = {4, 'M', 'I', 'F', '1', 0, 0, 0, 1, 0, 0, 0, 1, 0, 0,
                             0, 0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0};
    LLVMFuzzerTestOneInput(pixels, sizeof pixels);
    for (unsigned trial = 0; trial < 64; trial++) {
        guint8 mutation[sizeof pixels];
        memcpy(mutation, pixels, sizeof pixels);
        mutation[g_rand_int_range(random, 1, sizeof pixels)] ^= g_rand_int_range(random, 1, 256);
        LLVMFuzzerTestOneInput(mutation, sizeof mutation);
    }
    vips_shutdown();
    return 0;
}
#endif
