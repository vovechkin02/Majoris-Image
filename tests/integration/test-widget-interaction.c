#include <gdk/x11/gdkx.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <majoris-image-gtk4.h>

enum {
    ANCHOR_X = 80,
    ANCHOR_Y = 70,
    LAYOUT_TIMEOUT_MS = 5000,
};

typedef struct {
    Display *display;
    GtkWindow *window;
} Input;

static GdkTexture *
create_gradient_texture (GdkColorState *state)
{
    enum { edge = 160 };
    guint8 *pixels = g_malloc (edge * edge * 4);
    for (unsigned y = 0; y < edge; y++) {
        for (unsigned x = 0; x < edge; x++) {
            guint8 *pixel = pixels + 4 * (y * edge + x);
            pixel[0] = x;
            pixel[1] = y;
            pixel[2] = 255;
            pixel[3] = 255;
        }
    }
    g_autoptr (GBytes) bytes = g_bytes_new_take (pixels, edge * edge * 4);
    g_autoptr (GdkMemoryTextureBuilder) builder = gdk_memory_texture_builder_new ();
    gdk_memory_texture_builder_set_width (builder, edge);
    gdk_memory_texture_builder_set_height (builder, edge);
    gdk_memory_texture_builder_set_stride (builder, edge * 4);
    gdk_memory_texture_builder_set_format (builder, GDK_MEMORY_R8G8B8A8);
    gdk_memory_texture_builder_set_color_state (builder, state);
    gdk_memory_texture_builder_set_bytes (builder, bytes);
    return gdk_memory_texture_builder_build (builder);
}

static void
flush_events (Input *input)
{
    XSync (input->display, False);
    const gint64 end = g_get_monotonic_time () + 120 * G_TIME_SPAN_MILLISECOND;
    do {
        while (g_main_context_iteration (NULL, FALSE)) {
            /* Drain queued X11 events before checking the resulting view state. */
        }
        g_usleep (2000);
    } while (g_get_monotonic_time () < end);
}

static void
fail_input_layout (GtkWidget *view, const char *description)
{
    g_error ("%s: mapped=%d, allocation=%dx%d logical pixels, anchor=(%d,%d)", description,
             gtk_widget_get_mapped (view), gtk_widget_get_width (view),
             gtk_widget_get_height (view), ANCHOR_X, ANCHOR_Y);
}

static gboolean
find_input_anchor (GtkWidget *window, GtkWidget *view, graphene_point_t *point)
{
    const graphene_point_t anchor = GRAPHENE_POINT_INIT (ANCHOR_X, ANCHOR_Y);
    if (!gtk_widget_get_mapped (view) || gtk_widget_get_width (view) <= ANCHOR_X
        || gtk_widget_get_height (view) <= ANCHOR_Y
        || !gtk_widget_compute_point (view, window, &anchor, point)) {
        return FALSE;
    }
    GtkWidget *hit = gtk_widget_pick (window, point->x, point->y, GTK_PICK_DEFAULT);
    return hit != NULL && (hit == view || gtk_widget_is_ancestor (hit, view));
}

static gboolean
layout_timed_out (gpointer data)
{
    *(gboolean *)data = TRUE;
    return G_SOURCE_REMOVE;
}

static void
wait_for_input_layout (GtkWindow *window, GtkScrolledWindow *scroll, GtkWidget *views[],
                       gsize view_count)
{
    gboolean timed_out = FALSE;
    guint watchdog = g_timeout_add (LAYOUT_TIMEOUT_MS, layout_timed_out, &timed_out);
    GtkAdjustment *vertical = gtk_scrolled_window_get_vadjustment (scroll);
    while (!timed_out) {
        gboolean ready
            = gtk_widget_get_mapped (GTK_WIDGET (window))
              && gtk_widget_get_mapped (GTK_WIDGET (scroll))
              && gtk_adjustment_get_page_size (vertical) > ANCHOR_Y
              && gtk_adjustment_get_upper (vertical) > gtk_adjustment_get_page_size (vertical);
        for (gsize index = 0; index < view_count && ready; index++) {
            graphene_point_t point;
            ready = find_input_anchor (GTK_WIDGET (window), views[index], &point);
        }
        if (ready) {
            g_source_remove (watchdog);
            return;
        }
        g_main_context_iteration (NULL, TRUE);
    }
    for (gsize index = 0; index < view_count; index++) {
        graphene_point_t point;
        if (!find_input_anchor (GTK_WIDGET (window), views[index], &point)) {
            char description[80];
            g_snprintf (description, sizeof description,
                        "View %" G_GSIZE_FORMAT " has no visible input anchor", index + 1);
            fail_input_layout (views[index], description);
        }
    }
    g_error (
        "Scroll layout did not become ready: window=%dx%d, viewport=%dx%d, page=%g, content=%g",
        gtk_widget_get_width (GTK_WIDGET (window)), gtk_widget_get_height (GTK_WIDGET (window)),
        gtk_widget_get_width (GTK_WIDGET (scroll)), gtk_widget_get_height (GTK_WIDGET (scroll)),
        gtk_adjustment_get_page_size (vertical), gtk_adjustment_get_upper (vertical));
}

static void
move_pointer (Input *input, GtkWidget *view)
{
    GtkWidget *window = GTK_WIDGET (input->window);
    graphene_point_t point;
    if (!find_input_anchor (window, view, &point)) {
        fail_input_layout (view, "Cannot inject pointer input");
    }
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));
    Window unused;
    int x;
    int y;
    g_assert_true (XTranslateCoordinates (input->display, gdk_x11_surface_get_xid (surface),
                                          DefaultRootWindow (input->display), 0, 0, &x, &y,
                                          &unused));
    // The GTK transform is logical; XTest targets the root window in device pixels.
    int scale = gdk_surface_get_scale_factor (surface);
    XTestFakeMotionEvent (input->display, -1, x + (int)(point.x * scale),
                          y + (int)(point.y * scale), CurrentTime);
    flush_events (input);
}

static void
inject_key_event (Input *input, KeySym symbol, gboolean press)
{
    XTestFakeKeyEvent (input->display, XKeysymToKeycode (input->display, symbol), press,
                       CurrentTime);
}

static void
scroll_wheel (Input *input, unsigned button, gboolean control)
{
    if (control) {
        inject_key_event (input, XK_Control_L, TRUE);
    }
    XTestFakeButtonEvent (input->display, button, True, CurrentTime);
    XTestFakeButtonEvent (input->display, button, False, CurrentTime);
    if (control) {
        inject_key_event (input, XK_Control_L, FALSE);
    }
    flush_events (input);
}

static void
press_key (Input *input, KeySym symbol, gboolean control)
{
    if (control) {
        inject_key_event (input, XK_Control_L, TRUE);
    }
    inject_key_event (input, symbol, TRUE);
    inject_key_event (input, symbol, FALSE);
    if (control) {
        inject_key_event (input, XK_Control_L, FALSE);
    }
    flush_events (input);
}

static void
sample_view_pixel (GtkWidget *view, float x, float y, guint8 pixel[4])
{
    GtkWidget *parent = gtk_widget_get_parent (view);
    GtkSnapshot *snapshot = gtk_snapshot_new ();
    gtk_widget_snapshot_child (parent, view, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node (snapshot);
    g_assert_nonnull (node);
    graphene_point_t local = GRAPHENE_POINT_INIT (x, y), point;
    g_assert_true (gtk_widget_compute_point (view, parent, &local, &point));
    graphene_rect_t viewport = GRAPHENE_RECT_INIT (point.x, point.y, 1, 1);
    GskRenderer *renderer = gtk_native_get_renderer (gtk_widget_get_native (view));
    g_autoptr (GdkTexture) capture = gsk_renderer_render_texture (renderer, node, &viewport);
    gsk_render_node_unref (node);
    gdk_texture_download (capture, pixel, 4);
    g_assert_cmpint (pixel[3], ==, 255);
}

static gboolean
observe_propagated_scroll (GtkEventControllerScroll *controller, double dx, double dy,
                           guint *received)
{
    (void)controller;
    if (dx || dy) {
        (*received)++;
    }
    return FALSE;
}

static gboolean
observe_propagated_key (GtkEventControllerKey *controller, guint keyval, guint code,
                        GdkModifierType modifiers, guint *received)
{
    (void)controller;
    (void)code;
    (void)modifiers;
    if (keyval == GDK_KEY_F6 || keyval == GDK_KEY_equal || keyval == GDK_KEY_Left) {
        (*received)++;
    }
    return FALSE;
}

static void
mark_texture_released (gpointer data, GObject *object)
{
    (void)object;
    *(gboolean *)data = TRUE;
}

static void
verify_external_texture_state_and_accessibility (void)
{
    g_autoptr (ImageKitView) view = g_object_ref_sink (image_kit_image_view_new ());
    GtkAccessible *accessible = GTK_ACCESSIBLE (view);
    gtk_test_accessible_assert_role (accessible, GTK_ACCESSIBLE_ROLE_IMG);
    gtk_accessible_update_property (accessible, GTK_ACCESSIBLE_PROPERTY_LABEL, "Product browser",
                                    -1);
    image_kit_image_view_set_accessible_description (view, "First product");
    gtk_test_accessible_assert_property (accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                         "First product");

    gboolean released = FALSE;
    GdkTexture *pq = create_gradient_texture (gdk_color_state_get_rec2100_pq ());
    g_object_weak_ref (G_OBJECT (pq), mark_texture_released, &released);
    image_kit_image_view_set_texture (view, pq);
    g_object_unref (pq);
    g_assert_false (released);
    gtk_test_accessible_assert_property (accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                         "First product");
    g_assert_cmpint (image_kit_image_view_get_image_width (view), ==, 160);
    g_assert_cmpint (image_kit_image_view_get_image_height (view), ==, 160);
    g_assert_true (image_kit_image_view_get_hdr_source (view));
    g_assert_null (image_kit_image_view_get_document (view));
    g_assert_null (image_kit_image_view_get_metadata (view));
    g_assert_cmpint (image_kit_image_view_get_presentation_status (view), ==,
                     IMAGE_KIT_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);
    g_assert_nonnull (
        g_strstr_len (image_kit_image_view_get_presentation_diagnostic (view), -1, "PQ"));
    image_kit_image_view_set_presentation_policy (view, IMAGE_KIT_PRESENTATION_POLICY_SDR);
    g_assert_cmpint (image_kit_image_view_get_presentation_status (view), ==,
                     IMAGE_KIT_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);

    /* The view owns the only reference at this point. */
    image_kit_image_view_set_texture (view, pq);
    g_assert_false (released);

    g_autoptr (GdkTexture) linear = create_gradient_texture (gdk_color_state_get_srgb_linear ());
    image_kit_image_view_set_texture (view, linear);
    g_assert_true (released);
    gtk_test_accessible_assert_property (accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                         "Image preview");
    gtk_test_accessible_assert_property (accessible, GTK_ACCESSIBLE_PROPERTY_LABEL,
                                         "Product browser");
    g_assert_cmpint (image_kit_image_view_get_presentation_status (view), ==,
                     IMAGE_KIT_PRESENTATION_STATUS_EXTERNAL_UNVERIFIED);
    g_assert_false (image_kit_image_view_get_hdr_source (view));
    image_kit_image_view_clear (view);
    g_assert_cmpstr (image_kit_image_view_get_accessible_description (view), ==, "");
    g_assert_false (
        gtk_test_accessible_has_property (accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION));
    g_assert_cmpint (image_kit_image_view_get_presentation_status (view), ==,
                     IMAGE_KIT_PRESENTATION_STATUS_NONE);
    gtk_test_accessible_assert_property (accessible, GTK_ACCESSIBLE_PROPERTY_LABEL,
                                         "Product browser");
}

static void
verify_input_routing_and_zoom (Display *display)
{
    g_autoptr (GtkBuilder) builder = gtk_builder_new_from_string (
        "<interface><object class='ImageKitView' id='files'>"
        "<property name='scroll-zoom-mode'>control</property>"
        "<property name='accessible-description'>Landscape photograph</property>"
        "</object></interface>",
        -1);
    ImageKitView *files = IMAGE_KIT_IMAGE_VIEW (gtk_builder_get_object (builder, "files"));
    ImageKitView *photos = image_kit_image_view_new ();
    ImageKitView *disabled = image_kit_image_view_new ();
    image_kit_image_view_set_scroll_zoom_mode (disabled, IMAGE_KIT_SCROLL_ZOOM_MODE_DISABLED);
    g_assert_cmpint (image_kit_image_view_get_scroll_zoom_mode (photos), ==,
                     IMAGE_KIT_SCROLL_ZOOM_MODE_ALWAYS);

    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (GTK_WIDGET (files), 220, 240);
    gtk_box_append (GTK_BOX (content), GTK_WIDGET (files));
    GtkWidget *filler = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (filler, -1, 1000);
    gtk_box_append (GTK_BOX (content), filler);
    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), content);
    gtk_widget_set_hexpand (scroll, TRUE);

    GtkWidget *layout = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX (layout), scroll);
    gtk_box_append (GTK_BOX (layout), GTK_WIDGET (photos));
    gtk_box_append (GTK_BOX (layout), GTK_WIDGET (disabled));
    GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    gtk_window_set_title (window, "GTK input and accessibility test");
    // The three views share this width. The 1000-pixel filler must overflow the scroll viewport.
    gtk_window_set_default_size (window, 780, 400);
    gtk_window_set_child (window, layout);
    gtk_window_present (window);
    Input input = { display, window };
    GtkWidget *views[] = { GTK_WIDGET (files), GTK_WIDGET (photos), GTK_WIDGET (disabled) };
    wait_for_input_layout (window, GTK_SCROLLED_WINDOW (scroll), views, G_N_ELEMENTS (views));
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));
    int scale = gdk_surface_get_scale_factor (surface);
    const char *configured_scale = g_getenv ("GDK_SCALE");
    if (g_strcmp0 (configured_scale, "1") == 0 || g_strcmp0 (configured_scale, "2") == 0) {
        g_assert_cmpint (scale, ==, configured_scale[0] - '0');
    }
    move_pointer (&input, GTK_WIDGET (files));
    GtkAdjustment *vertical = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
    scroll_wheel (&input, 5, FALSE);
    g_assert_cmpfloat (gtk_adjustment_get_value (vertical), >, 0);
    gtk_adjustment_set_value (vertical, 0);
    flush_events (&input);

    g_autoptr (GdkTexture) srgb = create_gradient_texture (gdk_color_state_get_srgb ());
    image_kit_image_view_set_texture (files, srgb);
    image_kit_image_view_set_texture (photos, srgb);
    image_kit_image_view_set_texture (disabled, srgb);
    gtk_test_accessible_assert_property (
        GTK_ACCESSIBLE (files), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Landscape photograph");
    g_assert_cmpint (image_kit_image_view_get_presentation_status (files), ==,
                     IMAGE_KIT_PRESENTATION_STATUS_SDR);
    image_kit_image_view_zoom_to (files, 2.5, -1, -1);
    scroll_wheel (&input, 5, FALSE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), ==, 2.5);
    g_assert_cmpfloat (gtk_adjustment_get_value (vertical), >, 0);
    gtk_adjustment_set_value (vertical, 0);
    flush_events (&input);
    scroll_wheel (&input, 4, TRUE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), >, 2.5);
    g_assert_cmpfloat (gtk_adjustment_get_value (vertical), ==, 0);
    guint propagated_scroll = 0;
    GtkEventController *parent_wheel
        = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect (parent_wheel, "scroll", G_CALLBACK (observe_propagated_scroll),
                      &propagated_scroll);
    gtk_widget_add_controller (content, parent_wheel);
    image_kit_image_view_set_minimum_zoom (files, 2.5);
    image_kit_image_view_zoom_to (files, 2.5, -1, -1);
    scroll_wheel (&input, 5, TRUE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), ==, 2.5);
    g_assert_cmpuint (propagated_scroll, ==, 1);
    scroll_wheel (&input, 7, TRUE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), ==, 2.5);
    g_assert_cmpuint (propagated_scroll, ==, 2);
    gtk_adjustment_set_value (vertical, 0);
    flush_events (&input);

    image_kit_image_view_set_allow_zoom (files, FALSE);
    scroll_wheel (&input, 5, FALSE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), ==, 2.5);
    g_assert_cmpfloat (gtk_adjustment_get_value (vertical), >, 0);
    gtk_adjustment_set_value (vertical, 0);
    flush_events (&input);
    guint propagated = 0;
    GtkEventController *keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (observe_propagated_key), &propagated);
    gtk_widget_add_controller (content, keys);
    g_assert_true (gtk_widget_grab_focus (GTK_WIDGET (files)));
    XSetInputFocus (display, gdk_x11_surface_get_xid (surface), RevertToParent, CurrentTime);
    flush_events (&input);
    g_assert_true (gtk_widget_has_focus (GTK_WIDGET (files)));
    press_key (&input, XK_equal, FALSE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), ==, 2.5);
    g_assert_cmpuint (propagated, ==, 1);
    image_kit_image_view_set_allow_zoom (files, TRUE);
    press_key (&input, XK_equal, FALSE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (files), >, 2.5);
    g_assert_cmpuint (propagated, ==, 1);
    press_key (&input, XK_equal, TRUE);
    press_key (&input, XK_F6, FALSE);
    g_assert_cmpuint (propagated, ==, 3);
    image_kit_image_view_set_allow_pan (files, FALSE);
    press_key (&input, XK_Left, FALSE);
    g_assert_cmpuint (propagated, ==, 4);
    image_kit_image_view_set_allow_pan (files, TRUE);
    press_key (&input, XK_Left, FALSE);
    g_assert_cmpuint (propagated, ==, 4);

    image_kit_image_view_set_nearest_neighbor (files, TRUE);
    image_kit_image_view_rotate_clockwise (files);
    image_kit_image_view_flip_horizontal (files);
    image_kit_image_view_flip_vertical (files);
    image_kit_image_view_pan_by (files, -24, 16);
    move_pointer (&input, GTK_WIDGET (files));
    guint8 before[4], after[4];
    // Sample the same anchor before and after zoom to detect transform drift.
    sample_view_pixel (GTK_WIDGET (files), ANCHOR_X, ANCHOR_Y, before);
    scroll_wheel (&input, 4, TRUE);
    sample_view_pixel (GTK_WIDGET (files), ANCHOR_X, ANCHOR_Y, after);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmpint (ABS (before[i] - after[i]), <=, 2);
    }

    double photo_zoom = image_kit_image_view_get_zoom (photos);
    move_pointer (&input, GTK_WIDGET (photos));
    scroll_wheel (&input, 4, FALSE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (photos), >, photo_zoom);
    double disabled_zoom = image_kit_image_view_get_zoom (disabled);
    move_pointer (&input, GTK_WIDGET (disabled));
    scroll_wheel (&input, 4, TRUE);
    g_assert_cmpfloat (image_kit_image_view_get_zoom (disabled), ==, disabled_zoom);
    g_assert_cmpint (image_kit_image_view_get_scroll_zoom_mode (files), ==,
                     IMAGE_KIT_SCROLL_ZOOM_MODE_CONTROL);
    gtk_window_destroy (window);
}

int
main (void)
{
    if (!gtk_init_check () || !GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
        return 77;
    }
    Display *display = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
    int event;
    int error;
    int major;
    int minor;
    if (!XTestQueryExtension (display, &event, &error, &major, &minor)) {
        return 77;
    }
    g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);
    image_kit_image_view_register_type ();
    verify_external_texture_state_and_accessibility ();
    verify_input_routing_and_zoom (display);
    return 0;
}
