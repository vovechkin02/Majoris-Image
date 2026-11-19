#pragma once

#include <gio/gio.h>

/**
 * image_jpeg_transform:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @target: Destination filename used for the completed output.
 * @orientation: Source EXIF orientation, in the range 1 through 8.
 * @turns: Number of clockwise 90-degree turns, in the range 0 through 3.
 * @width: (out): Decoded width of the transformed JPEG in pixels.
 * @height: (out): Decoded height of the transformed JPEG in pixels.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Applies a perfect JPEG coefficient transform without recompression; rejects partial MCU
 * transforms.
 *
 * Returns: TRUE for a perfect coefficient transform; FALSE with a GError on failure.
 */
gboolean image_jpeg_transform(const char *source, const char *target, int orientation,
                              int turns, int *width, int *height, GError **error);
