#include <glib.h>

GBytes *__real_g_bytes_new_take(gpointer data, gsize size);
GBytes *__wrap_g_bytes_new_take(gpointer data, gsize size);

GBytes *__wrap_g_bytes_new_take(gpointer data, gsize size)
{
    gint64 started = g_get_monotonic_time();
    GBytes *bytes = __real_g_bytes_new_take(data, size);
    gint64 elapsed = g_get_monotonic_time() - started;

    /* The allocation and completed-output validation precede this ownership transfer. */
    g_print("ImageKit-profile gbytes-construction %" G_GINT64_FORMAT " %zu\n", elapsed, size);
    return bytes;
}
