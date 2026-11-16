/* Stop the test process after the pending PNG is written, before rename. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
fsync (int fd)
{
    int (*next_fsync) (int) = dlsym (RTLD_NEXT, "fsync");
    const char *gate = getenv ("IMAGEKIT_PUBLICATION_GATE");
    if (gate) {
        char descriptor[64], path[4096];
        snprintf (descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
        ssize_t size = readlink (descriptor, path, sizeof path - 1);
        const char suffix[] = ".png.pending";
        if (size >= (ssize_t)sizeof suffix - 1
            && !memcmp (path + size - sizeof suffix + 1, suffix, sizeof suffix - 1)) {
            int marker = open (gate, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (marker < 0) {
                _exit (112);
            }
            close (marker);
            for (;;) {
                pause ();
            }
        }
    }
    return next_fsync (fd);
}
