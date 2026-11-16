#ifndef SEQUENCE_TIMELINE_H
#define SEQUENCE_TIMELINE_H

#include <stdint.h>

#include <glib.h>

#define SEQUENCE_FRAME_LIMIT 10000

typedef struct {
    int sample; // -1 is the transparent interval specified by an empty edit.
    uint64_t start;
    uint64_t duration;
} PresentationFrame;

G_GNUC_INTERNAL
gboolean sequence_verify_track_timing(const char *path, uint32_t track_id, uint32_t movie_scale,
                                      uint32_t track_scale, uint64_t movie_duration, GArray *frames,
                                      gboolean *uses_libheif);

#endif
