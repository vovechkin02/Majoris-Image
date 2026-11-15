#pragma once
#include <gtk/gtk.h>
G_BEGIN_DECLS
GdkTexture *majoris_image_texture_new(GBytes *bytes, int width, int height, gsize stride);
/* Samples are straight-alpha RGBA float32 and already encoded in state. */
GdkTexture *majoris_image_texture_new_float_rgba(GBytes *bytes, int width, int height,
                                                gsize stride, GdkColorState *state);
const char *majoris_image_texture_external_description(GdkTexture *texture,
                                                        gboolean *sdr, gboolean *hdr);
int majoris_image_hdr_output(GtkWidget *widget);
G_END_DECLS
