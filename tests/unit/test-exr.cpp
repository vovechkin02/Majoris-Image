#include <cstring>
#include <vector>

#include <glib/gstdio.h>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTileDescription.h>
#include <OpenEXR/ImfTiledOutputPart.h>

#include "bridge.h"

namespace Imf = OPENEXR_IMF_NAMESPACE;
namespace Imath = IMATH_NAMESPACE;
static void create_exr_fixture(const char *path, float white = 203)
{
    Imf::Header headers[] = {Imf::Header(32, 16), Imf::Header(32, 16)};
    for (int p = 0; p < 2; p++) {
        headers[p].setName(p == 0 ? "Scanline HDR" : "Tiled HDR");
        headers[p].setType(p == 0 ? Imf::SCANLINEIMAGE : Imf::TILEDIMAGE);
        for (const char *name : {"R", "G", "B", "A"}) {
            headers[p].channels().insert(name, Imf::Channel(Imf::FLOAT));
        }
        headers[p].insert("chromaticities",
                          Imf::ChromaticitiesAttribute(Imf::Chromaticities(
                              {.708f, .292f}, {.170f, .797f}, {.131f, .046f}, {.3127f, .3290f})));
        headers[p].insert("whiteLuminance", Imf::FloatAttribute(white));
    }
    headers[0].dataWindow() = Imath::Box2i({-4, -2}, {27, 13});
    headers[1].setTileDescription(Imf::TileDescription(8, 8));
    Imf::MultiPartOutputFile output(path, headers, 2);
    for (int p = 0; p < 2; p++) {
        std::vector<float> data(32 * 16 * 4);
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 32; x++) {
                float *pixel = data.data() + (y * 32 + x) * 4;
                pixel[0] = (p == 0 ? 2 : 4.1234567f) + .01f * x + .001f * y;
                pixel[1] = .02f;
                pixel[2] = .01f;
                pixel[3] = .75f;
            }
        }
        Imf::FrameBuffer frame;
        const char *names[] = {"R", "G", "B", "A"};
        for (int c = 0; c < 4; c++) {
            frame.insert(names[c],
                         Imf::Slice::Make(Imf::FLOAT, data.data() + c, headers[p].dataWindow(),
                                          sizeof(float) * 4, sizeof(float) * 4 * 32));
        }
        if (p == 0) {
            Imf::OutputPart part(output, p);
            part.setFrameBuffer(frame);
            part.writePixels(16);
        } else {
            Imf::TiledOutputPart part(output, p);
            part.setFrameBuffer(frame);
            part.writeTiles(0, part.numXTiles() - 1, 0, part.numYTiles() - 1);
        }
    }
}

static double sample_pixel_channel(VipsImage *image, int x, int y, int channel)
{
    double *pixel = nullptr;
    int length = 0;
    g_assert_cmpint(vips_getpoint(image, &pixel, &length, x, y, nullptr), ==, 0);
    g_assert_cmpint(length, >, channel);
    double value = pixel[channel];
    g_free(pixel);
    return value;
}

static void test_exr_region_decoding()
{
    GError *error = nullptr;
    char *dir = g_dir_make_tmp("majoris-image-exr-parts-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "multipart.exr", nullptr);
    create_exr_fixture(path);
    VipsImage *first = image_codec_exr_image(path, 0, &error);
    g_assert_no_error(error);
    g_assert_nonnull(first);
    int pages = 0;
    g_assert_cmpint(vips_image_get_int(first, "n-pages", &pages), ==, 0);
    g_assert_cmpint(pages, ==, 2);
    g_assert_cmpfloat_with_epsilon(sample_pixel_channel(first, 16, 8, 0), 2.21f, 1e-6);
    g_assert_cmpfloat(sample_pixel_channel(first, 31, 15, 3), ==, 0);
    VipsImage *linear = image_codec_to_linear(first, &error);
    g_assert_no_error(error);
    double expected = 1.660491 * 2.21 - .587641 * .02 - .07285 * .01;
    g_assert_cmpfloat_with_epsilon(sample_pixel_channel(linear, 16, 8, 0), expected, .005);
    g_assert_cmpfloat(sample_pixel_channel(linear, 16, 8, 1), <, 0);
    g_object_unref(linear);
    g_object_unref(first);
    VipsImage *second = image_codec_exr_image(path, 1, &error);
    g_assert_no_error(error);
    g_assert_nonnull(second);
    g_assert_cmpfloat_with_epsilon(sample_pixel_channel(second, 20, 10, 0), 4.3334567f, 1e-6);
    g_assert_cmpfloat_with_epsilon(sample_pixel_channel(second, 20, 10, 3), .75, 1e-6);
    g_object_unref(second);
    create_exr_fixture(path, 100);
    first = image_codec_exr_image(path, 0, &error);
    g_assert_no_error(error);
    linear = image_codec_to_linear(first, &error);
    g_assert_no_error(error);
    g_assert_cmpfloat_with_epsilon(sample_pixel_channel(linear, 16, 8, 0), expected * 100 / 203,
                                   .005);
    g_object_unref(linear);
    g_object_unref(first);
    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--fixture")) {
        create_exr_fixture(argv[2]);
        return 0;
    }
    g_test_init(&argc, &argv, nullptr);
    VIPS_INIT("majoris-image-exr-parts-tests");
    vips_cache_set_max(0);
    g_test_add_func("/exr/regions/multipart-float-color", test_exr_region_decoding);
    return g_test_run();
}
