#define _GNU_SOURCE

#include "worker-output-fault.h"

#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

typedef ssize_t (*ReadFunction)(int fd, void *buffer, size_t size);

static GMutex fault_lock;
static gboolean armed;
static gboolean triggered;
static GCancellable *read_cancel;

void worker_output_fault_arm(GCancellable *cancel)
{
    g_mutex_lock(&fault_lock);
    armed = TRUE;
    triggered = FALSE;
    g_set_object(&read_cancel, cancel);
    g_mutex_unlock(&fault_lock);
}

gboolean worker_output_fault_finish(void)
{
    g_mutex_lock(&fault_lock);
    gboolean result = triggered;
    armed = FALSE;
    g_clear_object(&read_cancel);
    g_mutex_unlock(&fault_lock);
    return result;
}

static gboolean is_pixel_output(int fd)
{
    char descriptor[64], path[4096];
    g_snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
    ssize_t count = readlink(descriptor, path, sizeof path - 1);
    if (count <= 0) {
        return FALSE;
    }
    path[count] = '\0';
    return g_str_has_suffix(path, "/output") && strstr(path, "/majoris-image-") != NULL;
}

/* Interpose in the test process to inject faults after the sandboxed worker exits. */
ssize_t read(int fd, void *buffer, size_t size)
{
    ReadFunction next_read = G_GNUC_EXTENSION(ReadFunction) dlsym(RTLD_NEXT, "read");
    g_mutex_lock(&fault_lock);
    gboolean inject = armed && is_pixel_output(fd);
    g_autoptr(GCancellable) cancel = NULL;
    if (inject) {
        armed = FALSE;
        triggered = TRUE;
        cancel = g_steal_pointer(&read_cancel);
    }
    g_mutex_unlock(&fault_lock);

    if (inject && cancel == NULL) {
        char descriptor[64];
        g_snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
        g_assert_cmpint(truncate(descriptor, 12), ==, 0);
    }
    ssize_t received = next_read(fd, buffer, size);
    if (cancel != NULL) {
        g_cancellable_cancel(cancel);
    }
    return received;
}
