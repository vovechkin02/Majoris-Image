#pragma once
#include <gio/gio.h>
#include <vips/vips.h>
G_BEGIN_DECLS
gboolean majoris_codec_develop_raw(const char *source, const char *target, GError **error);
double majoris_codec_pq_to_linear(double value);
double majoris_codec_hlg_to_linear(double value);
void majoris_codec_annotate_heif(VipsImage *image, const char *source);
void majoris_codec_tag_srgb(VipsImage *image);
void majoris_codec_tag_linear(VipsImage *image);
const char *majoris_codec_worker_path(void);
char *majoris_codec_raw_info(const char *source);
gboolean majoris_codec_raw_preview(const char *source, const char *target, GError **error);
gboolean majoris_codec_develop_raw_options(const char *source, const char *target, int quality, gboolean camera_wb, GError **error);
VipsImage *majoris_codec_to_linear(VipsImage *image, GError **error);
VipsImage *majoris_codec_convert_profile(VipsImage *linear, const char *profile_name, int depth, GError **error);
gboolean majoris_codec_encoder_available(const char *extension);
gboolean majoris_codec_ultra_probe(const char *path);
VipsImage *majoris_codec_ultra_decode(const char *path, gboolean sdr, GError **error);
gboolean majoris_codec_ultra_encode(VipsImage *image, const char *path, int format, int quality, GError **error);
char **majoris_codec_desktop_baseline(void);
char *majoris_codec_codec_json(void);
char *majoris_codec_image_details(VipsImage *image);
gboolean majoris_codec_sync_stream(GOutputStream *stream, GError **error);
gboolean majoris_codec_exr_encode(VipsImage *image, const char *path, int depth, GError **error);
VipsImage *majoris_codec_exr_image(const char *path, int page, GError **error);
gboolean majoris_codec_jxl_encode(VipsImage *image, const char *path, int depth, int quality, gboolean lossless, GError **error);
VipsImage *majoris_codec_svg_region(const char *path, double x, double y, double width, double height, int source_width, int source_height, int size, GError **error);

gboolean majoris_codec_write_pixels(VipsImage *image, const char *path, GError **error);
int majoris_codec_frame_duration(VipsImage *image, int page);
VipsImage *majoris_codec_sequence(const char *path, int frame, gboolean avif, int known_count, GError **error);
VipsImage *majoris_codec_read_pixels(const char *path, GError **error);
G_END_DECLS
