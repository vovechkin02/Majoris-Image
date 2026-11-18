#pragma once

#include <gio/gio.h>

gboolean image_jpeg_transform(const char *source, const char *target, int orientation,
                              int turns, int *width, int *height, GError **error);
