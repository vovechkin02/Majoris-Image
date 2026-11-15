#include <majoris-image-gtk4.h>
#include <gdk/x11/gdkx.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

typedef struct {
    Display *display;
    GtkWindow *window;
} Input;

static GdkTexture *make_texture(GdkColorState *state)
{
    enum { edge = 160 };
    guint8 *pixels = g_malloc(edge * edge * 4);
    for (unsigned y = 0; y < edge; y++) {
        for (unsigned x = 0; x < edge; x++) {
            guint8 *pixel = pixels + 4 * (y * edge + x);
            pixel[0] = x;
            pixel[1] = y;
            pixel[2] = 255;
            pixel[3] = 255;
        }
    }
    g_autoptr(GBytes) bytes = g_bytes_new_take(pixels, edge * edge * 4);
    g_autoptr(GdkMemoryTextureBuilder) builder = gdk_memory_texture_builder_new();
    gdk_memory_texture_builder_set_width(builder, edge);
    gdk_memory_texture_builder_set_height(builder, edge);
    gdk_memory_texture_builder_set_stride(builder, edge * 4);
    gdk_memory_texture_builder_set_format(builder, GDK_MEMORY_R8G8B8A8);
    gdk_memory_texture_builder_set_color_state(builder, state);
    gdk_memory_texture_builder_set_bytes(builder, bytes);
    return gdk_memory_texture_builder_build(builder);
}

static void flush_events(Input *input)
{
    XSync(input->display, False);
    const gint64 end = g_get_monotonic_time() + 120 * G_TIME_SPAN_MILLISECOND;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(2000);
    } while (g_get_monotonic_time() < end);
}

static void move_pointer(Input *input, GtkWidget *view)
{
    GtkWidget *window = GTK_WIDGET(input->window);
    graphene_point_t center = GRAPHENE_POINT_INIT(80, 70), point;
    g_assert_true(gtk_widget_compute_point(view, window, &center, &point));
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    Window unused;
    int x, y;
    XTranslateCoordinates(input->display, gdk_x11_surface_get_xid(surface),
                          DefaultRootWindow(input->display), 0, 0, &x, &y, &unused);
    int scale = gdk_surface_get_scale_factor(surface);
    XTestFakeMotionEvent(input->display, -1, x + (int)(point.x * scale),
                        y + (int)(point.y * scale), CurrentTime);
    flush_events(input);
}

static void key(Input *input, KeySym symbol, gboolean press)
{
    XTestFakeKeyEvent(input->display, XKeysymToKeycode(input->display, symbol), press, CurrentTime);
}

static void wheel(Input *input, unsigned button, gboolean control)
{
    if (control) key(input, XK_Control_L, TRUE);
    XTestFakeButtonEvent(input->display, button, True, CurrentTime);
    XTestFakeButtonEvent(input->display, button, False, CurrentTime);
    if (control) key(input, XK_Control_L, FALSE);
    flush_events(input);
}

static void press_key(Input *input, KeySym symbol, gboolean control)
{
    if (control) key(input, XK_Control_L, TRUE);
    key(input, symbol, TRUE);
    key(input, symbol, FALSE);
    if (control) key(input, XK_Control_L, FALSE);
    flush_events(input);
}

static void sample_view(GtkWidget *view, float x, float y, guint8 pixel[4])
{
    GtkWidget *parent = gtk_widget_get_parent(view);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gtk_widget_snapshot_child(parent, view, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    graphene_point_t local = GRAPHENE_POINT_INIT(x, y), point;
    g_assert_true(gtk_widget_compute_point(view, parent, &local, &point));
    graphene_rect_t viewport = GRAPHENE_RECT_INIT(point.x, point.y, 1, 1);
    GskRenderer *renderer = gtk_native_get_renderer(gtk_widget_get_native(view));
    g_autoptr(GdkTexture) capture = gsk_renderer_render_texture(renderer, node, &viewport);
    gsk_render_node_unref(node);
    gdk_texture_download(capture, pixel, 4);
    g_assert_cmpint(pixel[3], ==, 255);
}

static gboolean parent_scroll(GtkEventControllerScroll *controller, double dx, double dy, guint *received)
{
    (void) controller;
    if (dx || dy) (*received)++;
    return FALSE;
}

static gboolean parent_key(GtkEventControllerKey *controller, guint keyval, guint code,
                           GdkModifierType modifiers, guint *received)
{
    (void) controller;
    (void) code;
    (void) modifiers;
    if (keyval == GDK_KEY_F6 || keyval == GDK_KEY_equal || keyval == GDK_KEY_Left)
        (*received)++;
    return FALSE;
}

static void finalized(gpointer data, GObject *object)
{
    (void) object;
    *(gboolean *)data = TRUE;
}

static void test_external(void)
{
    g_autoptr(MajorisImageView) view = g_object_ref_sink(majoris_image_image_view_new());
    GtkAccessible *accessible = GTK_ACCESSIBLE(view);
    gtk_test_accessible_assert_role(accessible, GTK_ACCESSIBLE_ROLE_IMG);
    gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL, "Product browser", -1);
    majoris_image_image_view_set_accessible_description(view, "First product");
    gtk_test_accessible_assert_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "First product");

    gboolean released = FALSE;
    GdkTexture *pq = make_texture(gdk_color_state_get_rec2100_pq());
    g_object_weak_ref(G_OBJECT(pq), finalized, &released);
    majoris_image_image_view_set_texture(view, pq);
    g_object_unref(pq);
    g_assert_false(released);
    gtk_test_accessible_assert_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "First product");
    g_assert_cmpint(majoris_image_image_view_get_image_width(view), ==, 160);
    g_assert_cmpint(majoris_image_image_view_get_image_height(view), ==, 160);
    g_assert_true(majoris_image_image_view_get_hdr_source(view));
    g_assert_null(majoris_image_image_view_get_document(view));
    g_assert_null(majoris_image_image_view_get_metadata(view));
    g_assert_cmpint(majoris_image_image_view_get_presentation_status(view), ==,
                    MAJORIS_IMAGE_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);
    g_assert_nonnull(g_strstr_len(majoris_image_image_view_get_presentation_diagnostic(view), -1, "PQ"));
    majoris_image_image_view_set_presentation_policy(view, MAJORIS_IMAGE_PRESENTATION_POLICY_SDR);
    g_assert_cmpint(majoris_image_image_view_get_presentation_status(view), ==,
                    MAJORIS_IMAGE_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);

    /* The view owns the only reference at this point. */
    majoris_image_image_view_set_texture(view, pq);
    g_assert_false(released);

    g_autoptr(GdkTexture) linear = make_texture(gdk_color_state_get_srgb_linear());
    majoris_image_image_view_set_texture(view, linear);
    g_assert_true(released);
    gtk_test_accessible_assert_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Image preview");
    gtk_test_accessible_assert_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL, "Product browser");
    g_assert_cmpint(majoris_image_image_view_get_presentation_status(view), ==,
                    MAJORIS_IMAGE_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);
    g_assert_false(majoris_image_image_view_get_hdr_source(view));
    majoris_image_image_view_clear(view);
    g_assert_cmpstr(majoris_image_image_view_get_accessible_description(view), ==, "");
    g_assert_false(gtk_test_accessible_has_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION));
    g_assert_cmpint(majoris_image_image_view_get_presentation_status(view), ==,
                    MAJORIS_IMAGE_PRESENTATION_STATUS_NONE);
    gtk_test_accessible_assert_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL, "Product browser");
}

int main(void)
{
    if (!gtk_init_check() || !GDK_IS_X11_DISPLAY(gdk_display_get_default())) return 77;
    Display *display = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    int event, error, major, minor;
    if (!XTestQueryExtension(display, &event, &error, &major, &minor)) return 77;
    g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, NULL);
    majoris_image_image_view_register_type();
    test_external();

    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_string(
        "<interface><object class='MajorisImageView' id='files'>"
        "<property name='scroll-zoom-mode'>control</property>"
        "<property name='accessible-description'>Landscape photograph</property>"
        "</object></interface>", -1);
    MajorisImageView *files = MAJORIS_IMAGE_IMAGE_VIEW(gtk_builder_get_object(builder, "files"));
    MajorisImageView *photos = majoris_image_image_view_new();
    MajorisImageView *disabled = majoris_image_image_view_new();
    majoris_image_image_view_set_scroll_zoom_mode(disabled, MAJORIS_IMAGE_SCROLL_ZOOM_MODE_DISABLED);
    g_assert_cmpint(majoris_image_image_view_get_scroll_zoom_mode(photos), ==, MAJORIS_IMAGE_SCROLL_ZOOM_MODE_ALWAYS);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(GTK_WIDGET(files), 220, 240);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(files));
    GtkWidget *filler = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(filler, -1, 1000);
    gtk_box_append(GTK_BOX(content), filler);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
    gtk_widget_set_hexpand(scroll, TRUE);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(layout), scroll);
    gtk_box_append(GTK_BOX(layout), GTK_WIDGET(photos));
    gtk_box_append(GTK_BOX(layout), GTK_WIDGET(disabled));
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_default_size(window, 780, 400);
    gtk_window_set_child(window, layout);
    gtk_window_present(window);
    Input input = {display, window};
    flush_events(&input);
    move_pointer(&input, GTK_WIDGET(files));
    GtkAdjustment *vertical = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    wheel(&input, 5, FALSE);
    g_assert_cmpfloat(gtk_adjustment_get_value(vertical), >, 0);
    gtk_adjustment_set_value(vertical, 0);
    flush_events(&input);

    g_autoptr(GdkTexture) srgb = make_texture(gdk_color_state_get_srgb());
    majoris_image_image_view_set_texture(files, srgb);
    majoris_image_image_view_set_texture(photos, srgb);
    majoris_image_image_view_set_texture(disabled, srgb);
    gtk_test_accessible_assert_property(GTK_ACCESSIBLE(files), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                        "Landscape photograph");
    g_assert_cmpint(majoris_image_image_view_get_presentation_status(files), ==,
                    MAJORIS_IMAGE_PRESENTATION_STATUS_SDR);
    majoris_image_image_view_zoom_to(files, 2.5, -1, -1);
    wheel(&input, 5, FALSE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), ==, 2.5);
    g_assert_cmpfloat(gtk_adjustment_get_value(vertical), >, 0);
    gtk_adjustment_set_value(vertical, 0);
    flush_events(&input);
    wheel(&input, 4, TRUE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), >, 2.5);
    g_assert_cmpfloat(gtk_adjustment_get_value(vertical), ==, 0);
    guint propagated_scroll = 0;
    GtkEventController *parent_wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(parent_wheel, "scroll", G_CALLBACK(parent_scroll), &propagated_scroll);
    gtk_widget_add_controller(content, parent_wheel);
    majoris_image_image_view_set_minimum_zoom(files, 2.5);
    majoris_image_image_view_zoom_to(files, 2.5, -1, -1);
    wheel(&input, 5, TRUE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), ==, 2.5);
    g_assert_cmpuint(propagated_scroll, ==, 1);
    wheel(&input, 7, TRUE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), ==, 2.5);
    g_assert_cmpuint(propagated_scroll, ==, 2);
    gtk_adjustment_set_value(vertical, 0);
    flush_events(&input);

    majoris_image_image_view_set_allow_zoom(files, FALSE);
    wheel(&input, 5, FALSE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), ==, 2.5);
    g_assert_cmpfloat(gtk_adjustment_get_value(vertical), >, 0);
    gtk_adjustment_set_value(vertical, 0);
    flush_events(&input);
    guint propagated = 0;
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(parent_key), &propagated);
    gtk_widget_add_controller(content, keys);
    g_assert_true(gtk_widget_grab_focus(GTK_WIDGET(files)));
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    XSetInputFocus(display, gdk_x11_surface_get_xid(surface), RevertToParent, CurrentTime);
    flush_events(&input);
    g_assert_true(gtk_widget_has_focus(GTK_WIDGET(files)));
    press_key(&input, XK_equal, FALSE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), ==, 2.5);
    g_assert_cmpuint(propagated, ==, 1);
    majoris_image_image_view_set_allow_zoom(files, TRUE);
    press_key(&input, XK_equal, FALSE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(files), >, 2.5);
    g_assert_cmpuint(propagated, ==, 1);
    press_key(&input, XK_equal, TRUE);
    press_key(&input, XK_F6, FALSE);
    g_assert_cmpuint(propagated, ==, 3);
    majoris_image_image_view_set_allow_pan(files, FALSE);
    press_key(&input, XK_Left, FALSE);
    g_assert_cmpuint(propagated, ==, 4);
    majoris_image_image_view_set_allow_pan(files, TRUE);
    press_key(&input, XK_Left, FALSE);
    g_assert_cmpuint(propagated, ==, 4);

    majoris_image_image_view_set_nearest_neighbour(files, TRUE);
    majoris_image_image_view_rotate_clockwise(files);
    majoris_image_image_view_flip_horizontal(files);
    majoris_image_image_view_flip_vertical(files);
    majoris_image_image_view_pan_by(files, -24, 16);
    move_pointer(&input, GTK_WIDGET(files));
    guint8 before[4], after[4];
    sample_view(GTK_WIDGET(files), 80, 70, before);
    wheel(&input, 4, TRUE);
    sample_view(GTK_WIDGET(files), 80, 70, after);
    for (unsigned i = 0; i < 4; i++) g_assert_cmpint(ABS(before[i] - after[i]), <=, 2);

    double photo_zoom = majoris_image_image_view_get_zoom(photos);
    move_pointer(&input, GTK_WIDGET(photos));
    wheel(&input, 4, FALSE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(photos), >, photo_zoom);
    double disabled_zoom = majoris_image_image_view_get_zoom(disabled);
    move_pointer(&input, GTK_WIDGET(disabled));
    wheel(&input, 4, TRUE);
    g_assert_cmpfloat(majoris_image_image_view_get_zoom(disabled), ==, disabled_zoom);
    g_assert_cmpint(majoris_image_image_view_get_scroll_zoom_mode(files), ==, MAJORIS_IMAGE_SCROLL_ZOOM_MODE_CONTROL);
    gtk_window_destroy(window);
    return 0;
}
