#include "bridge.h"
#include <libraw/libraw.h>
#include <json-glib/json-glib.h>

static void string_member(JsonBuilder *b, const char *name, const char *value) {
    json_builder_set_member_name(b, name); json_builder_add_string_value(b, value ? value : "");
}
static void int_member(JsonBuilder *b, const char *name, int value) {
    json_builder_set_member_name(b, name); json_builder_add_int_value(b, value);
}
/* Header inspection and thumbnail extraction run only inside the decoder sandbox. */
char *majoris_codec_raw_info(const char *source) {
    libraw_data_t *raw = libraw_init(0);
    JsonBuilder *b = json_builder_new(); json_builder_begin_object(b);
    gboolean recognized = raw && libraw_open_file(raw, source) == LIBRAW_SUCCESS && raw->idata.raw_count > 0;
    json_builder_set_member_name(b, "raw"); json_builder_add_boolean_value(b, recognized);
    if (recognized) {
        string_member(b, "camera-make", raw->idata.make); string_member(b, "camera-model", raw->idata.model);
        string_member(b, "lens", raw->lens.Lens); int_member(b, "width", raw->sizes.width); int_member(b, "height", raw->sizes.height);
        int_member(b, "raw-width", raw->sizes.raw_width); int_member(b, "raw-height", raw->sizes.raw_height);
        int_member(b, "colors", raw->idata.colors); int_member(b, "cfa", raw->idata.filters);
        int_member(b, "white-level", raw->color.maximum); int_member(b, "black-level", raw->color.black);
        int flip = raw->sizes.flip;
        static const int exif_orientation[] = {1, 2, 4, 3, 5, 8, 6, 7};
        int_member(b, "orientation", flip);
        if (flip >= 0 && flip < 8) int_member(b, "exif-orientation", exif_orientation[flip]);
        unsigned white = raw->color.maximum;
        int bits = 0;
        while (white) { bits++; white >>= 1; }
        int_member(b, "sensor-white-level-bits", bits);
        int_member(b, "preview-byte-count", raw->thumbnail.tlength);
        int_member(b, "iso", raw->other.iso_speed);
        json_builder_set_member_name(b, "aperture"); json_builder_add_double_value(b, raw->other.aperture);
        json_builder_set_member_name(b, "shutter"); json_builder_add_double_value(b, raw->other.shutter);
        json_builder_set_member_name(b, "focal-length"); json_builder_add_double_value(b, raw->other.focal_len);
        json_builder_set_member_name(b, "white-balance"); json_builder_begin_array(b);
        for (int c = 0; c < 4; c++) json_builder_add_double_value(b, raw->color.cam_mul[c]);
        json_builder_end_array(b);
    }
    json_builder_end_object(b); JsonNode *node = json_builder_get_root(b);
    char *text = json_to_string(node, FALSE); json_node_free(node); g_object_unref(b);
    if (raw) libraw_close(raw);
    return text;
}
gboolean majoris_codec_raw_preview(const char *source, const char *target, GError **error) {
    libraw_data_t *raw = libraw_init(0);
    if (!raw) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Cannot allocate RAW preview"); return FALSE; }
    int result = libraw_open_file(raw, source);
    if (!result) result = libraw_unpack_thumb(raw);
    if (!result) result = libraw_dcraw_thumb_writer(raw, target);
    libraw_close(raw);
    if (result) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, libraw_strerror(result));
    return result == 0;
}
