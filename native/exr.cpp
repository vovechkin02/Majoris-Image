#include <cmath>
#include <exception>
#include <vector>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>

#include "bridge.h"

// Scanline output bounds extra memory to one source row. Values remain linear
// sRGB, including negative wide-gamut samples and highlights above SDR white.
gboolean image_codec_exr_encode(VipsImage *image, const char *path, int depth, GError **error)
{
    VipsImage *linear = image_codec_to_linear(image, error);
    if (!linear) {
        return FALSE;
    }
    VipsRegion *region = vips_region_new(linear);
    gboolean result = FALSE;
    try {
        namespace Imf = OPENEXR_IMF_NAMESPACE;
        Imf::Header header(linear->Xsize, linear->Ysize);
        const bool alpha = linear->Bands == 4;
        const char *names[] = {"R", "G", "B", "A"};
        for (int c = 0; c < (alpha ? 4 : 3); c++) {
            header.channels().insert(names[c], Imf::Channel(depth == 16 ? Imf::HALF : Imf::FLOAT));
        }
        header.insert("chromaticities", Imf::ChromaticitiesAttribute(Imf::Chromaticities()));
        header.insert("whiteLuminance", Imf::FloatAttribute(203.0f));
        Imf::OutputFile output(path, header);
        std::vector<float> row(static_cast<size_t>(linear->Xsize) * linear->Bands);
        std::vector<IMATH_NAMESPACE::half> half_row(depth == 16 ? row.size() : 0);
        Imf::FrameBuffer frame;
        for (int c = 0; c < (alpha ? 4 : 3); c++) {
            frame.insert(names[c],
                         depth == 16
                             ? Imf::Slice(Imf::HALF, reinterpret_cast<char *>(half_row.data() + c),
                                          sizeof(IMATH_NAMESPACE::half) * linear->Bands, 0)
                             : Imf::Slice(Imf::FLOAT, reinterpret_cast<char *>(row.data() + c),
                                          sizeof(float) * linear->Bands, 0));
        }
        output.setFrameBuffer(frame);
        for (int y = 0; y < linear->Ysize; y++) {
            VipsRect rect = {0, y, linear->Xsize, 1};
            if (vips_region_prepare(region, &rect)) {
                throw std::runtime_error(vips_error_buffer());
            }
            memcpy(row.data(), VIPS_REGION_ADDR(region, 0, y), row.size() * sizeof(float));
            if (depth == 16) {
                for (size_t i = 0; i < row.size(); i++) {
                    if (!std::isfinite(row[i]) || std::fabs(row[i]) > 65504) {
                        throw std::runtime_error(
                            "Pixel exceeds half-float range; select 32-bit float");
                    }
                    half_row[i] = row[i];
                }
            }
            output.writePixels(1);
        }
        result = TRUE;
    } catch (const std::exception &e) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "OpenEXR: %s", e.what());
    }
    g_object_unref(region);
    g_object_unref(linear);
    return result;
}
