#include <gtk/gtk.h>
#include <majoris-image-gtk4.h>

int main(void)
{
    if (!gtk_init_check()) return 77;
    g_type_ensure(MAJORIS_IMAGE_TYPE_IMAGE_VIEW);
    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_string(
        "<interface><object class='MajorisImageView' id='view'>"
        "<property name='fit-mode'>contain</property>"
        "<property name='allow-zoom'>true</property>"
        "</object></interface>", -1);
    GObject *view = gtk_builder_get_object(builder, "view");
    return MAJORIS_IMAGE_IS_IMAGE_VIEW(view) ? 0 : 1;
}
