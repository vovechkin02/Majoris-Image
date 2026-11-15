#include "image-texture.h"

GdkTexture *majoris_image_texture_new(GBytes *bytes, int width, int height, gsize stride)
{
    GdkMemoryTextureBuilder *builder = gdk_memory_texture_builder_new();
    gdk_memory_texture_builder_set_width(builder, width);
    gdk_memory_texture_builder_set_height(builder, height);
    gdk_memory_texture_builder_set_stride(builder, stride);
    gdk_memory_texture_builder_set_format(builder, GDK_MEMORY_R32G32B32A32_FLOAT);
    // Presentation supplies either tone-mapped or extended-range linear sRGB.
    // Both are straight-alpha float data in the same primaries and transfer.
    gdk_memory_texture_builder_set_color_state(builder, gdk_color_state_get_srgb_linear());
    gdk_memory_texture_builder_set_bytes(builder, bytes);
    GdkTexture *texture = gdk_memory_texture_builder_build(builder);
    g_object_unref(builder);
    return texture;
}


int majoris_image_hdr_output(GtkWidget *widget)
{
    GtkNative *native = gtk_widget_get_native(widget);
    if (!native) return 0;
    GdkSurface *surface = gtk_native_get_surface(native);
    GskRenderer *renderer = gtk_native_get_renderer(native);
    if (!surface || !gdk_surface_get_mapped(surface) || !renderer ||
        !gsk_renderer_is_realized(renderer)) return 0;
    // GTK publicly exposes a realized renderer and colour-managed float
    // textures. Those capabilities alone do not prove HDR output or scanout.
    return 1;
}
