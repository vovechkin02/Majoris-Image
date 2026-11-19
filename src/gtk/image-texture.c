#include "image-texture.h"

/** Builds a color-aware GDK texture from straight-alpha RGBA float pixels. */
GdkTexture *image_texture_new_float_rgba(GBytes *bytes, int width, int height, gsize stride,
                                         GdkColorState *state)
{
    const gsize pixel = 4 * sizeof(float);
    if (!bytes || !state || width <= 0 || height <= 0 || (gsize)width > G_MAXSIZE / pixel) {
        return NULL;
    }
    gsize row = (gsize)width * pixel;
    if (stride < row || stride % sizeof(float) || (gsize)height > G_MAXSIZE / stride ||
        (gsize)height * stride > g_bytes_get_size(bytes)) {
        return NULL;
    }

    GdkMemoryTextureBuilder *builder = gdk_memory_texture_builder_new();
    gdk_memory_texture_builder_set_width(builder, width);
    gdk_memory_texture_builder_set_height(builder, height);
    gdk_memory_texture_builder_set_stride(builder, stride);
    gdk_memory_texture_builder_set_format(builder, GDK_MEMORY_R32G32B32A32_FLOAT);
    gdk_memory_texture_builder_set_color_state(builder, state);
    gdk_memory_texture_builder_set_bytes(builder, bytes);
    GdkTexture *texture = gdk_memory_texture_builder_build(builder);
    g_object_unref(builder);
    return texture;
}

/** Builds a GDK texture from the standard ImageKit float32 pixel layout. */
GdkTexture *image_texture_new(GBytes *bytes, int width, int height, gsize stride)
{
    return image_texture_new_float_rgba(bytes, width, height, stride,
                                        gdk_color_state_get_srgb_linear());
}

/** Describes an existing texture color state and HDR/SDR capability. */
const char *image_texture_external_description(GdkTexture *texture, gboolean *sdr, gboolean *hdr)
{
    GdkColorState *state = gdk_texture_get_color_state(texture);
    *sdr = gdk_color_state_equal(state, gdk_color_state_get_srgb());
    *hdr = gdk_color_state_equal(state, gdk_color_state_get_rec2100_pq()) ||
           gdk_color_state_equal(state, gdk_color_state_get_rec2100_linear());

    if (*sdr) {
        return "External sRGB texture.";
    }
    if (gdk_color_state_equal(state, gdk_color_state_get_srgb_linear())) {
        return "External linear sRGB texture. Range is unverified; GTK renders the original "
               "texture.";
    }
    if (gdk_color_state_equal(state, gdk_color_state_get_rec2100_pq())) {
        return "External Rec. 2100 PQ texture. GTK renders the original texture; HDR output is "
               "unverified.";
    }
    if (*hdr) {
        return "External linear Rec. 2100 texture. GTK renders the original texture; HDR output is "
               "unverified.";
    }
    return "External texture with another color state. GTK renders the original texture; output is "
           "unverified.";
}

/** Reports whether the GTK widget output supports HDR presentation. */
int image_hdr_output(GtkWidget *widget)
{
    GtkNative *native = gtk_widget_get_native(widget);
    if (!native) {
        return 0;
    }
    GdkSurface *surface = gtk_native_get_surface(native);
    GskRenderer *renderer = gtk_native_get_renderer(native);
    if (!surface || !gdk_surface_get_mapped(surface) || !renderer ||
        !gsk_renderer_is_realized(renderer)) {
        return 0;
    }
    // GTK publicly exposes a realized renderer and color-managed float
    // textures. Those capabilities alone do not prove HDR output or scanout.
    return 1;
}
