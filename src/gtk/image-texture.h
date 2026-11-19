#pragma once
#include <gtk/gtk.h>
G_BEGIN_DECLS
/**
 * image_texture_new:
 * @bytes: Reference-counted pixel bytes; retained by the created texture.
 * @width: Width in pixels, a normalized region width, or an output width as declared.
 * @height: Height in pixels, a normalized region height, or an output height as declared.
 * @stride: Distance between consecutive image rows in bytes.
 *
 * Builds a GDK texture from the standard ImageKit float32 pixel layout.
 *
 * Returns: A new GdkTexture reference; the caller must unref it.
 */
GdkTexture *image_texture_new(GBytes *bytes, int width, int height, gsize stride);
/* Samples are straight-alpha RGBA float32 and already encoded in state. */
/**
 * image_texture_new_float_rgba:
 * @bytes: Reference-counted pixel bytes; retained by the created texture.
 * @width: Width in pixels, a normalized region width, or an output width as declared.
 * @height: Height in pixels, a normalized region height, or an output height as declared.
 * @stride: Distance between consecutive image rows in bytes.
 * @state: Output color state associated with the supplied float samples.
 *
 * Builds a color-aware GDK texture from straight-alpha RGBA float pixels.
 *
 * Returns: A new GdkTexture reference; the caller must unref it.
 */
GdkTexture *image_texture_new_float_rgba(GBytes *bytes, int width, int height, gsize stride,
                                         GdkColorState *state);
/**
 * image_texture_external_description:
 * @sdr: Whether the texture/output uses the recognized SDR state.
 * @hdr: Output flag indicating an HDR-capable texture color state.
 *
 * Describes an existing texture color state and HDR/SDR capability.
 *
 * Returns: A borrowed static description string; do not free it.
 */
const char *image_texture_external_description(GdkTexture *texture, gboolean *sdr, gboolean *hdr);
/**
 * image_hdr_output:
 * @widget: GTK widget whose realized output surface is inspected.
 *
 * Reports whether the GTK widget output supports HDR presentation.
 *
 * Returns: Zero if suitable output support is unavailable, or the detected capability value.
 */
int image_hdr_output(GtkWidget *widget);
G_END_DECLS
