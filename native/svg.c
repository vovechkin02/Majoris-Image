#include "bridge.h"
#include <librsvg/rsvg.h>
#include <math.h>

VipsImage *image_codec_svg_region(const char *path, double x, double y, double width, double height, int source_width, int source_height, int size, GError **error) {
    if (width<=0 || height<=0 || source_width<=0 || source_height<=0 || size<1 || size>4096) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid SVG region"); return NULL;
    }
    RsvgHandle *handle=rsvg_handle_new_from_file(path,error); if (!handle) return NULL;
    double logical_width=width*source_width, logical_height=height*source_height;
    double scale=size/MAX(logical_width,logical_height);
    int w=MAX(1,(int)ceil(logical_width*scale)), h=MAX(1,(int)ceil(logical_height*scale));
    cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
    cairo_t *cr=cairo_create(surface); cairo_translate(cr,-x*source_width*scale,-y*source_height*scale);
    RsvgRectangle viewport={0,0,source_width*scale,source_height*scale};
    gboolean rendered=rsvg_handle_render_document(handle,cr,&viewport,error);
    cairo_destroy(cr); g_object_unref(handle);
    if (!rendered || cairo_surface_status(surface)!=CAIRO_STATUS_SUCCESS) {
        if (rendered) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"SVG region allocation failed");
        cairo_surface_destroy(surface); return NULL;
    }
    cairo_surface_flush(surface); int stride=cairo_image_surface_get_stride(surface);
    const guint8 *data=cairo_image_surface_get_data(surface); guint8 *rgba=g_malloc_n((size_t)w*h,4);
    for (int row=0;row<h;row++) for (int column=0;column<w;column++) {
        guint32 pixel=((const guint32 *)(data+(size_t)row*stride))[column];
        guint8 *out=rgba+((size_t)row*w+column)*4; unsigned alpha=pixel>>24;
        out[0]=alpha ? MIN(255,((pixel>>16)&255)*255/alpha) : 0;
        out[1]=alpha ? MIN(255,((pixel>>8)&255)*255/alpha) : 0;
        out[2]=alpha ? MIN(255,(pixel&255)*255/alpha) : 0; out[3]=alpha;
    }
    VipsImage *image=vips_image_new_from_memory_copy(rgba,(size_t)w*h*4,w,h,4,VIPS_FORMAT_UCHAR);
    g_free(rgba); cairo_surface_destroy(surface);
    if (image) image->Type=VIPS_INTERPRETATION_sRGB;
    else g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,vips_error_buffer());
    return image;
}
