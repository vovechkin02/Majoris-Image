#include <gtk/gtk.h>
#include <majoris-image-gtk4.h>

typedef struct {
    GMainLoop *loop;
    int status;
} Example;

static void fail(Example *example, GError *error)
{
    if (g_error_matches(error, MAJORIS_IMAGE_IMAGE_ERROR, MAJORIS_IMAGE_IMAGE_ERROR_UNSUPPORTED) ||
        g_error_matches(error, MAJORIS_IMAGE_IMAGE_ERROR, MAJORIS_IMAGE_IMAGE_ERROR_CODEC_MISSING)) {
        g_printerr("This selection has no supported decoder.\n");
    } else {
        g_printerr("%s\n", error->message);
    }
    example->status = 1;
    g_main_loop_quit(example->loop);
}

static void region_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Example *example = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(MajorisImagePixelBuffer) pixels =
        majoris_image_image_document_render_region_finish(MAJORIS_IMAGE_IMAGE_DOCUMENT(object), result, &error);
    if (!pixels) {
        fail(example, error);
        return;
    }
    g_print("Rendered %d x %d linear pixels\n", majoris_image_pixel_buffer_get_width(pixels),
            majoris_image_pixel_buffer_get_height(pixels));
    g_main_loop_quit(example->loop);
}

static void image_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Example *example = data;
    MajorisImageView *view = MAJORIS_IMAGE_IMAGE_VIEW(object);
    g_autoptr(GError) error = NULL;
    majoris_image_image_view_load_file_finish(view, result, &error);
    if (error) {
        fail(example, error);
        return;
    }
    MajorisImageImageDocument *document = majoris_image_image_view_get_document(view);
    MajorisImageImageMetadata *metadata = majoris_image_image_document_get_metadata(document);
    g_print("%s\n%s\n", majoris_image_image_metadata_get_json(metadata),
            majoris_image_image_view_get_presentation_diagnostic(view));
    if (majoris_image_image_view_get_animated(view)) {
        majoris_image_image_view_play(view);
        majoris_image_image_view_pause(view);
        majoris_image_image_view_step_frame(view);
    }

    int width = MIN(512, majoris_image_image_document_get_width(document));
    int height = MIN(512, majoris_image_image_document_get_height(document));
    majoris_image_image_document_render_region(document, 0, 0, width, height,
        MAX(width, height), FALSE, NULL, region_ready, example);
}

static void thumbnail_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Example *example = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) png = majoris_image_thumbnail_service_generate_finish(
        MAJORIS_IMAGE_THUMBNAIL_SERVICE(object), result, &error);
    if (!png) {
        fail(example, error);
        return;
    }
    g_main_loop_quit(example->loop);
}

int main(int argc, char **argv)
{
    if (!gtk_init_check()) return 77;
    g_type_ensure(MAJORIS_IMAGE_TYPE_IMAGE_VIEW);
    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_string(
        "<interface><object class='MajorisImageView' id='view'>"
        "<property name='fit-mode'>contain</property>"
        "<property name='allow-zoom'>true</property>"
        "</object></interface>", -1);
    GObject *view = gtk_builder_get_object(builder, "view");
    if (!MAJORIS_IMAGE_IS_IMAGE_VIEW(view)) return 1;
    if (argc == 1) return 0;

    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GFile) file = g_file_new_for_commandline_arg(argv[1]);
    g_autoptr(MajorisImageImageSource) source = majoris_image_image_source_new(file);
    g_autoptr(MajorisImageThumbnailService) thumbnails = majoris_image_thumbnail_service_new();
    Example example = {loop, 0};
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_child(window, GTK_WIDGET(view));
    gtk_window_present(window);
    majoris_image_image_view_load_file(MAJORIS_IMAGE_IMAGE_VIEW(view), file, NULL, NULL, image_ready, &example);
    g_main_loop_run(loop);
    if (!example.status) {
        majoris_image_thumbnail_service_generate(thumbnails, source, 128, NULL, thumbnail_ready, &example);
        g_main_loop_run(loop);
    }
    gtk_window_destroy(window);
    return example.status;
}
