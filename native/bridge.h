#pragma once
#include <gio/gio.h>

#include <vips/vips.h>
G_BEGIN_DECLS
/**
 * image_codec_develop_raw:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @target: Destination filename used for the completed output.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Develops a RAW source using the default RAW conversion settings.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_develop_raw(const char *source, const char *target, GError **error);
/**
 * image_codec_pq_to_linear:
 * @value: Transfer-function input value in the expected normalized domain.
 *
 * Converts a normalized PQ transfer value into linear light.
 */
double image_codec_pq_to_linear(double value);
/**
 * image_codec_hlg_to_linear:
 * @value: Transfer-function input value in the expected normalized domain.
 *
 * Converts an HLG transfer value into linear light.
 */
double image_codec_hlg_to_linear(double value);
/**
 * image_codec_annotate_heif:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 *
 * Imports HEIF bit depth and color profile metadata into the decoded image.
 */
void image_codec_annotate_heif(VipsImage *image, const char *source);
/**
 * image_codec_tag_srgb:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 *
 * Attaches sRGB color characteristics to the image without changing its pixels.
 */
void image_codec_tag_srgb(VipsImage *image);
/**
 * image_codec_tag_linear:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 *
 * Tags the output image with linear-light sRGB profile metadata.
 */
void image_codec_tag_linear(VipsImage *image);
/**
 * image_codec_worker_path:
 *
 * Returns the trusted installed worker executable path used for native operations.
 *
 * Returns: A borrowed, process-lifetime worker executable path; do not free it.
 */
const char *image_codec_worker_path(void);
/**
 * image_codec_raw_info:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 *
 * Returns JSON metadata describing the RAW source or available camera information.
 *
 * Returns: An allocated JSON string; the caller must release it with g_free().
 */
char *image_codec_raw_info(const char *source);
/**
 * image_codec_raw_preview:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @target: Destination filename used for the completed output.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Extracts an available embedded RAW preview into the specified file.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_raw_preview(const char *source, const char *target, GError **error);
/**
 * image_codec_develop_raw_options:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @target: Destination filename used for the completed output.
 * @quality: Encoder or RAW-development quality setting, interpreted by the codec.
 * @camera_wb: Whether RAW development should use camera white balance.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Develops a RAW source using the requested quality and camera white balance settings.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_develop_raw_options(const char *source, const char *target, int quality,
                                         gboolean camera_wb, GError **error);
/**
 * image_codec_to_linear:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Converts image pixels to linear-light float samples while retaining color metadata.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_to_linear(VipsImage *image, GError **error);
/**
 * image_codec_convert_profile:
 * @linear: Input image containing linear-light samples.
 * @profile_name: Target color profile identifier, including the source-profile policy.
 * @depth: Requested sample precision in bits per channel.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Converts linear-light samples into the requested output profile and precision.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_convert_profile(VipsImage *linear, const char *profile_name, int depth,
                                       GError **error);
/**
 * image_codec_encoder_available:
 * @extension: Output format extension used to select an encoder.
 *
 * Reports whether the codec can encode the requested output extension.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_encoder_available(const char *extension);
/**
 * image_codec_ultra_probe:
 * @path: Filesystem path to the source, temporary output or target resource.
 *
 * Checks whether a source has a supported Ultra HDR gain-map representation.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_ultra_probe(const char *path);
/**
 * image_codec_ultra_decode:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @sdr: Whether the texture/output uses the recognized SDR state.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Decodes Ultra HDR content with HDR or SDR presentation as requested.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_ultra_decode(const char *path, gboolean sdr, GError **error);
/**
 * image_codec_ultra_encode:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @path: Filesystem path to the source, temporary output or target resource.
 * @format: Target codec or output image format.
 * @quality: Encoder or RAW-development quality setting, interpreted by the codec.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Encodes an image into the requested Ultra HDR output representation.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_ultra_encode(VipsImage *image, const char *path, int format, int quality,
                                  GError **error);
/**
 * image_codec_desktop_baseline:
 *
 * Returns the baseline MIME types supported by the desktop image pipeline.
 *
 * Returns: A NULL-terminated owned MIME list; release it with g_strfreev().
 */
char **image_codec_desktop_baseline(void);
/**
 * image_codec_catalog_json:
 *
 * Generates the installed codec capability catalog in JSON.
 *
 * Returns: An allocated JSON string; release it with g_free().
 */
char *image_codec_catalog_json(void);
/**
 * image_codec_image_details:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 *
 * Extracts normalized image metadata and color details as JSON.
 *
 * Returns: An allocated JSON description; release it with g_free().
 */
char *image_codec_image_details(VipsImage *image);
/**
 * image_codec_sync_stream:
 * @stream: Writable GIO output stream whose completion must be verified.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Flushes and synchronizes an output stream before successful publication.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_sync_stream(GOutputStream *stream, GError **error);
/**
 * image_codec_exr_encode:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @path: Filesystem path to the source, temporary output or target resource.
 * @depth: Requested sample precision in bits per channel.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Writes a linear-light OpenEXR image with the requested half/float precision.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_exr_encode(VipsImage *image, const char *path, int depth, GError **error);
/**
 * image_codec_exr_image:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @page: Zero-based page or view index.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Creates a lazily decoded OpenEXR view while enforcing source dimensions and bounds.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_exr_image(const char *path, int page, GError **error);
/**
 * image_codec_jxl_encode:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @path: Filesystem path to the source, temporary output or target resource.
 * @depth: Requested sample precision in bits per channel.
 * @quality: Encoder or RAW-development quality setting, interpreted by the codec.
 * @lossless: Whether the encoder must preserve lossless compression.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Encodes a JPEG XL image using the requested bit depth, quality and lossless policy.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_jxl_encode(VipsImage *image, const char *path, int depth, int quality,
                                gboolean lossless, GError **error);
/**
 * image_codec_svg_region:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @x: Horizontal position in source-relative coordinates.
 * @y: Vertical position in source-relative coordinates.
 * @width: Width in pixels, a normalized region width, or an output width as declared.
 * @height: Height in pixels, a normalized region height, or an output height as declared.
 * @source_width: Source image width in pixels.
 * @source_height: Source image height in pixels.
 * @size: Requested output edge or buffer size according to the operation.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Renders a bounded region of SVG source to the requested raster size.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_svg_region(const char *path, double x, double y, double width, double height,
                                  int source_width, int source_height, int size, GError **error);

/**
 * image_codec_write_pixels:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @path: Filesystem path to the source, temporary output or target resource.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Writes a validated little-endian pixel transport buffer for the isolated worker.
 *
 * Returns: TRUE if the requested operation succeeds; FALSE if it fails or is unsupported.
 */
gboolean image_codec_write_pixels(VipsImage *image, const char *path, GError **error);
/**
 * image_codec_frame_duration:
 * @image: Image whose pixel data or metadata is inspected or transformed.
 * @page: Zero-based page or view index.
 *
 * Returns the animation frame duration in milliseconds for the selected page.
 *
 * Returns: An integer status or measurement as defined by the operation.
 */
int image_codec_frame_duration(VipsImage *image, int page);
/**
 * image_codec_sequence:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @frame: Zero-based animation frame index.
 * @avif: Whether to select the AVIF sequence-decoding path.
 * @known_count: Known bounded number of source sequence frames.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Opens the specified HEIF/AVIF animation frame with sequence timing metadata.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_sequence(const char *path, int frame, gboolean avif, int known_count,
                                GError **error);
/**
 * image_codec_read_pixels:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Maps and validates pixel transport bytes before constructing an image.
 *
 * Returns: A newly referenced VipsImage or NULL on error; the caller unrefs it.
 */
VipsImage *image_codec_read_pixels(const char *path, GError **error);
G_END_DECLS
