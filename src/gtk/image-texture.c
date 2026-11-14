#include "image-texture.h"

GdkTexture *majoris_image_texture_new(GBytes *bytes, int width, int height, gsize stride)
{
    GdkMemoryTextureBuilder *builder = gdk_memory_texture_builder_new();
    gdk_memory_texture_builder_set_width(builder, width);
    gdk_memory_texture_builder_set_height(builder, height);
    gdk_memory_texture_builder_set_stride(builder, stride);
    gdk_memory_texture_builder_set_format(builder, GDK_MEMORY_R32G32B32A32_FLOAT);
    gdk_memory_texture_builder_set_color_state(builder, gdk_color_state_get_srgb_linear());
    gdk_memory_texture_builder_set_bytes(builder, bytes);
    GdkTexture *texture = gdk_memory_texture_builder_build(builder);
    g_object_unref(builder);
    return texture;
}
