#pragma once
#include <gtk/gtk.h>
G_BEGIN_DECLS
GdkTexture *majoris_image_texture_new(GBytes *bytes, int width, int height, gsize stride);
G_END_DECLS
