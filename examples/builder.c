#include <gtk/gtk.h>

#include <majoris-image-gtk4.h>

typedef struct {
    GMainLoop *loop;
    int status;
} Example;

static void fail(Example *example, GError *error)
{
    if (g_error_matches(error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_UNSUPPORTED) ||
        g_error_matches(error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_CODEC_MISSING)) {
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
    g_autoptr(ImageKitPixelBuffer) pixels = image_kit_image_document_render_region_finish(
        IMAGE_KIT_IMAGE_DOCUMENT(object), result, &error);
    if (!pixels) {
        fail(example, error);
        return;
    }
    g_print("Rendered %d x %d linear pixels\n", image_kit_pixel_buffer_get_width(pixels),
            image_kit_pixel_buffer_get_height(pixels));
    g_main_loop_quit(example->loop);
}

static void image_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Example *example = data;
    ImageKitView *view = IMAGE_KIT_IMAGE_VIEW(object);
    g_autoptr(GError) error = NULL;
    image_kit_image_view_load_file_finish(view, result, &error);
    if (error) {
        fail(example, error);
        return;
    }
    ImageKitImageDocument *document = image_kit_image_view_get_document(view);
    ImageKitImageMetadata *metadata = image_kit_image_document_get_metadata(document);
    g_print("%s\n%s\n", image_kit_image_metadata_get_json(metadata),
            image_kit_image_view_get_presentation_diagnostic(view));
    if (image_kit_image_view_get_animated(view)) {
        image_kit_image_view_play(view);
        image_kit_image_view_pause(view);
        image_kit_image_view_step_frame(view);
    }

    int width = MIN(512, image_kit_image_document_get_width(document));
    int height = MIN(512, image_kit_image_document_get_height(document));
    image_kit_image_document_render_region(document, 0, 0, width, height, MAX(width, height), FALSE,
                                           NULL, region_ready, example);
}

static void thumbnail_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Example *example = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) png = image_kit_thumbnail_service_generate_finish(
        IMAGE_KIT_THUMBNAIL_SERVICE(object), result, &error);
    if (!png) {
        fail(example, error);
        return;
    }
    g_main_loop_quit(example->loop);
}

int main(int argc, char **argv)
{
    if (!gtk_init_check()) {
        return 77;
    }
    g_type_ensure(IMAGE_KIT_TYPE_IMAGE_VIEW);
    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_string(
        "<interface><object class='ImageKitView' id='view'>"
        "<property name='fit-mode'>contain</property>"
        "<property name='allow-zoom'>true</property>"
        "<property name='scroll-zoom-mode'>control</property>"
        "<property name='accessible-description'>Selected image</property>"
        "</object></interface>",
        -1);
    GObject *view = gtk_builder_get_object(builder, "view");
    if (!IMAGE_KIT_IS_IMAGE_VIEW(view)) {
        return 1;
    }
    ImageKitView *image_view = IMAGE_KIT_IMAGE_VIEW(view);
    if (image_kit_image_view_get_scroll_zoom_mode(image_view) !=
            IMAGE_KIT_SCROLL_ZOOM_MODE_CONTROL ||
        gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(view)) != GTK_ACCESSIBLE_ROLE_IMG ||
        g_strcmp0(image_kit_image_view_get_accessible_description(image_view), "Selected image") !=
            0) {
        return 1;
    }
    if (argc == 1) {
        return 0;
    }

    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GFile) file = g_file_new_for_commandline_arg(argv[1]);
    g_autoptr(ImageKitImageSource) source = image_kit_image_source_new(file);
    g_autoptr(ImageKitThumbnailService) thumbnails = image_kit_thumbnail_service_new();
    Example example = {loop, 0};
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(window, "GtkBuilder image viewer");
    gtk_window_set_default_size(window, 640, 480);
    gtk_window_set_child(window, GTK_WIDGET(view));
    gtk_window_present(window);
    image_kit_image_view_load_file(IMAGE_KIT_IMAGE_VIEW(view), file, NULL, NULL, image_ready,
                                   &example);
    g_main_loop_run(loop);
    if (!example.status) {
        image_kit_thumbnail_service_generate(thumbnails, source, 128, NULL, thumbnail_ready,
                                             &example);
        g_main_loop_run(loop);
    }
    gtk_window_destroy(window);
    return example.status;
}
