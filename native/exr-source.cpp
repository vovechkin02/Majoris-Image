#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <json-glib/json-glib.h>
#include <lcms2.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfTiledInputPart.h>

#include "bridge.h"

namespace Exr = OPENEXR_IMF_NAMESPACE;
namespace ExrMath = IMATH_NAMESPACE;
namespace
{
/** Identifies one OpenEXR part/channel view without decoding its samples. */
struct View {
    int part;
    std::string name;
    std::array<std::string, 4> channels;
};
/** Enumerates usable OpenEXR parts and channel views, rejecting excessive view counts. */
std::vector<View> list_views(Exr::MultiPartInputFile &file)
{
    std::vector<View> views;
    for (int p = 0; p < file.parts(); p++) {
        const auto &header = file.header(p);
        const auto &channels = header.channels();
        if (header.hasType() && Exr::isDeepData(header.type())) {
            continue;
        }
        std::set<std::string> prefixes;
        for (auto i = channels.begin(); i != channels.end(); ++i) {
            std::string name = i.name(), prefix;
            if (name == "R") {
                prefix = "";
            } else if (name.size() > 2 && name.substr(name.size() - 2) == ".R") {
                prefix = name.substr(0, name.size() - 1);
            } else {
                continue;
            }
            if (channels.findChannel(prefix + "G") && channels.findChannel(prefix + "B")) {
                prefixes.insert(prefix);
            }
        }
        std::string label = header.hasName() ? header.name() : "Part " + std::to_string(p + 1);
        for (const auto &prefix : prefixes) {
            View view = {p,
                         label +
                             (prefix.empty() ? "" : " — " + prefix.substr(0, prefix.size() - 1)),
                         {prefix + "R", prefix + "G", prefix + "B",
                          channels.findChannel(prefix + "A") ? prefix + "A" : ""}};
            views.push_back(view);
        }
        if (prefixes.empty() && channels.begin() != channels.end()) {
            std::string channel = channels.findChannel("Y") ? "Y" : channels.begin().name();
            views.push_back({p,
                             label + " — " + channel,
                             {channel, channel, channel, channels.findChannel("A") ? "A" : ""}});
        }
        if (views.size() > 4096) {
            throw std::runtime_error("EXR contains too many views for the memory budget");
        }
    }
    if (views.empty()) {
        throw std::runtime_error("Unsupported deep-only OpenEXR content");
    }
    return views;
}
/** Owns lazy OpenEXR part access and bounded scratch planes for Vips rendering. */
struct Source {
    Exr::MultiPartInputFile file;
    std::vector<View> views;
    View view;
    ExrMath::Box2i data, display;
    std::unique_ptr<Exr::InputPart> scanline;
    std::unique_ptr<Exr::TiledInputPart> tiled;
    std::array<std::vector<float>, 4> buffers;
    std::array<int, 4> plane_index;
    std::mutex mutex;
    int buffer_width = 0, buffer_height = 1;
    /** Opens the indexed OpenEXR view and validates dimensions and channel sampling. */
    explicit Source(const char *path, int index) : file(path, 2), views(list_views(file))
    {
        if (index < 0 || static_cast<size_t>(index) >= views.size()) {
            throw std::runtime_error("Invalid OpenEXR view index");
        }
        view = views[index];
        const auto &header = file.header(view.part);
        data = header.dataWindow();
        display = header.displayWindow();
        int64_t width = static_cast<int64_t>(display.max.x) - display.min.x + 1,
                height = static_cast<int64_t>(display.max.y) - display.min.y + 1;
        int64_t data_width = static_cast<int64_t>(data.max.x) - data.min.x + 1;
        if (width < 1 || height < 1 || width > G_MAXINT || height > G_MAXINT || data_width < 1 ||
            data_width > 2000000) {
            throw std::runtime_error("OpenEXR dimensions exceed the region decoder memory budget");
        }
        if (header.hasTileDescription()) {
            tiled = std::make_unique<Exr::TiledInputPart>(file, view.part);
            buffer_width = tiled->tileXSize();
            buffer_height = tiled->tileYSize();
            if (buffer_width < 1 || buffer_height < 1 ||
                static_cast<uint64_t>(buffer_width) * buffer_height > 4194304) {
                throw std::runtime_error("OpenEXR tile exceeds the memory budget");
            }
        } else {
            scanline = std::make_unique<Exr::InputPart>(file, view.part);
            buffer_width = static_cast<int>(data_width);
        }
        for (int c = 0; c < 4; c++) {
            plane_index[c] = c;
            if (view.channels[c].empty()) {
                continue;
            }
            const auto *channel = header.channels().findChannel(view.channels[c]);
            if (channel->xSampling != 1 || channel->ySampling != 1) {
                throw std::runtime_error("Unsupported subsampled OpenEXR channel");
            }
            for (int previous = 0; previous < c; previous++) {
                if (view.channels[previous] == view.channels[c]) {
                    plane_index[c] = previous;
                    break;
                }
            }
            if (plane_index[c] == c) {
                buffers[c].resize(static_cast<size_t>(buffer_width) * buffer_height);
            }
        }
    }
/** Builds a channel-mapped OpenEXR frame buffer over the currently allocated sample planes. */
    Exr::FrameBuffer frame(int y, bool tile)
    {
        Exr::FrameBuffer frame;
        for (int c = 0; c < 4; c++) {
            if (!view.channels[c].empty() && plane_index[c] == c) {
                frame.insert(view.channels[c],
                             Exr::Slice::Make(Exr::FLOAT, buffers[c].data(),
                                              ExrMath::V2i(tile ? 0 : data.min.x, tile ? 0 : y),
                                              buffer_width, buffer_height, sizeof(float),
                                              sizeof(float) * buffer_width, 1, 1, 0, tile, tile));
            }
        }
        return frame;
    }
/** Copies one decoded OpenEXR sample to an interleaved RGBA destination. */
    void pixel(float *target, size_t index)
    {
        for (int c = 0; c < 3; c++) {
            target[c] = buffers[plane_index[c]][index];
        }
        target[3] = view.channels[3].empty() ? 1 : buffers[plane_index[3]][index];
    }
};
/** Renders the requested OpenEXR region under a lock while preserving image bounds. */
int generate(VipsRegion *output, void *, void *, void *context, gboolean *)
{
    auto *source = static_cast<Source *>(context);
    std::lock_guard<std::mutex> lock(source->mutex);
    const VipsRect area = output->valid;
    try {
        for (int y = area.top; y < VIPS_RECT_BOTTOM(&area); y++) {
            memset(VIPS_REGION_ADDR(output, area.left, y), 0,
                   static_cast<size_t>(area.width) * 4 * sizeof(float));
        }
        int left = std::max(area.left + source->display.min.x, source->data.min.x),
            right =
                std::min(VIPS_RECT_RIGHT(&area) + source->display.min.x, source->data.max.x + 1);
        int top = std::max(area.top + source->display.min.y, source->data.min.y),
            bottom =
                std::min(VIPS_RECT_BOTTOM(&area) + source->display.min.y, source->data.max.y + 1);
        if (left >= right || top >= bottom) {
            return 0;
        }
        if (source->scanline) {
            for (int y = top; y < bottom; y++) {
                source->scanline->setFrameBuffer(source->frame(y, false));
                source->scanline->readPixels(y);
                auto *target = reinterpret_cast<float *>(VIPS_REGION_ADDR(
                    output, left - source->display.min.x, y - source->display.min.y));
                for (int x = left; x < right; x++, target += 4) {
                    source->pixel(target, static_cast<size_t>(x - source->data.min.x));
                }
            }
        } else {
            source->tiled->setFrameBuffer(source->frame(0, true));
            int tw = source->buffer_width, th = source->buffer_height;
            for (int ty = (top - source->data.min.y) / th;
                 ty <= (bottom - 1 - source->data.min.y) / th; ty++) {
                for (int tx = (left - source->data.min.x) / tw;
                     tx <= (right - 1 - source->data.min.x) / tw; tx++) {
                    source->tiled->readTile(tx, ty);
                    int origin_x = source->data.min.x + tx * tw,
                        origin_y = source->data.min.y + ty * th;
                    for (int y = std::max(top, origin_y); y < std::min(bottom, origin_y + th);
                         y++) {
                        auto *target = reinterpret_cast<float *>(VIPS_REGION_ADDR(
                            output, std::max(left, origin_x) - source->display.min.x,
                            y - source->display.min.y));
                        for (int x = std::max(left, origin_x); x < std::min(right, origin_x + tw);
                             x++, target += 4) {
                            source->pixel(target,
                                          static_cast<size_t>(y - origin_y) * tw + x - origin_x);
                        }
                    }
                }
            }
        }
        return 0;
    } catch (const std::exception &error) {
        vips_error("OpenEXR", "%s", error.what());
        return -1;
    }
}

/** Releases the OpenEXR source retained as a libvips generation context. */
void destroy(void *context)
{
    delete static_cast<Source *>(context);
}
/** Serializes OpenEXR window coordinates for image metadata. */
std::string window_string(const ExrMath::Box2i &window)
{
    return std::to_string(window.min.x) + ", " + std::to_string(window.min.y) + " — " +
           std::to_string(window.max.x) + ", " + std::to_string(window.max.y);
}

/** Builds an ICC profile from OpenEXR color attributes for downstream color management. */
void profile(VipsImage *image, const Exr::Header &header)
{
    if (Exr::hasChromaticities(header)) {
        const auto &ch = Exr::chromaticities(header);
        cmsCIExyY white = {ch.white.x, ch.white.y, 1};
        cmsCIExyYTRIPLE primaries = {
            {ch.red.x, ch.red.y, 1}, {ch.green.x, ch.green.y, 1}, {ch.blue.x, ch.blue.y, 1}};
        cmsToneCurve *curve = cmsBuildGamma(nullptr, 1), *curves[] = {curve, curve, curve};
        cmsHPROFILE icc = cmsCreateRGBProfile(&white, &primaries, curves);
        cmsFreeToneCurve(curve);
        if (!icc) {
            throw std::runtime_error("Invalid OpenEXR chromaticities profile");
        }
        cmsMLU *description = cmsMLUalloc(nullptr, 1);
        cmsMLUsetASCII(description, "en", "US", "OpenEXR linear RGB chromaticities");
        cmsWriteTag(icc, cmsSigProfileDescriptionTag, description);
        cmsMLUfree(description);
        cmsUInt32Number size = 0;
        cmsSaveProfileToMem(icc, nullptr, &size);
        std::vector<unsigned char> bytes(size);
        if (cmsSaveProfileToMem(icc, bytes.data(), &size)) {
            vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, bytes.data(), size);
        }
        cmsCloseProfile(icc);
    }
    if (Exr::hasWhiteLuminance(header)) {
        double white = Exr::whiteLuminance(header);
        if (!std::isfinite(white) || white <= 0 || white > 1e9) {
            throw std::runtime_error("Invalid OpenEXR white luminance metadata");
        }
        vips_image_set_double(image, "majoris-image-linear-white-nits", white);
    }
}
} // namespace
/** Creates a lazily decoded OpenEXR view while enforcing source dimensions and bounds. */
VipsImage *image_codec_exr_image(const char *path, int page, GError **error)
{
    VipsImage *image = nullptr;
    try {
        auto source = std::make_unique<Source>(path, page);
        const auto &header = source->file.header(source->view.part);
        image = vips_image_new();
        vips_image_init_fields(image, source->display.max.x - source->display.min.x + 1,
                               source->display.max.y - source->display.min.y + 1, 4,
                               VIPS_FORMAT_FLOAT, VIPS_CODING_NONE, VIPS_INTERPRETATION_scRGB, 1,
                               1);
        if (vips_image_pipelinev(image, VIPS_DEMAND_STYLE_SMALLTILE, nullptr)) {
            throw std::runtime_error(vips_error_buffer());
        }
        int depth = 16;
        std::string channels;
        for (auto channel = header.channels().begin(); channel != header.channels().end();
             ++channel) {
            if (!channels.empty()) {
                channels += ", ";
            }
            channels += channel.name();
            channels += channel.channel().type == Exr::FLOAT  ? " (float)"
                        : channel.channel().type == Exr::HALF ? " (half)"
                                                              : " (uint)";
            if (channel.channel().type != Exr::HALF) {
                depth = 32;
            }
        }
        vips_image_set_string(image, "vips-loader", "openexr-native");
        vips_image_set_int(image, "n-pages", source->views.size());
        vips_image_set_int(image, "majoris-image-bit-depth", depth);
        vips_image_set_string(image, "exr-channels", channels.c_str());
        vips_image_set_string(image, "exr-view", source->view.name.c_str());
        vips_image_set_string(image, "exr-data-window", window_string(source->data).c_str());
        vips_image_set_string(image, "exr-display-window", window_string(source->display).c_str());
        const char *compressions[] = {"None",  "RLE", "ZIPS", "ZIP",  "PIZ",
                                      "PXR24", "B44", "B44A", "DWAA", "DWAB"};
        int compression = header.compression();
        vips_image_set_string(image, "exr-compression",
                              compression >= 0 && compression < 10 ? compressions[compression]
                                                                   : "Unknown");
        vips_image_set_string(image, "exr-sample-format",
                              depth == 16 ? "16-bit half float" : "32-bit channels");
        JsonArray *names = json_array_new();
        for (const auto &view : source->views) {
            json_array_add_string_element(names, view.name.c_str());
        }
        JsonNode *node = json_node_new(JSON_NODE_ARRAY);
        json_node_take_array(node, names);
        char *labels = json_to_string(node, FALSE);
        vips_image_set_string(image, "majoris-image-image-names", labels);
        g_free(labels);
        json_node_free(node);
        profile(image, header);
        Source *context = source.release();
        g_object_set_data_full(G_OBJECT(image), "majoris-image-exr-source", context, destroy);
        if (vips_image_generate(image, nullptr, generate, nullptr, nullptr, context)) {
            throw std::runtime_error(vips_error_buffer());
        }
        return image;
    } catch (const std::exception &exception) {
        g_clear_object(&image);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "OpenEXR: %s", exception.what());
        return nullptr;
    }
}
