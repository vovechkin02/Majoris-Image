#ifndef SEQUENCE_TIMELINE_H
#define SEQUENCE_TIMELINE_H

#include <stdint.h>

#include <glib.h>

#define SEQUENCE_FRAME_LIMIT 10000

/** One bounded ISO BMFF presentation interval; sample -1 denotes a transparent edit. */
typedef struct {
    int sample; // -1 is the transparent interval specified by an empty edit.
    uint64_t start;
    uint64_t duration;
} PresentationFrame;

/**
 * sequence_verify_track_timing:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @track_id: Container track identifier to verify.
 * @movie_scale: Timescale of the ISO BMFF movie presentation.
 * @track_scale: Timescale of the selected media track.
 * @movie_duration: Declared duration in movie timescale ticks.
 * @frames: Array of presentation-frame intervals filled by the verifier.
 * @uses_libheif: Output flag indicating whether timeline decoding remains with libheif.
 *
 * Validates AVIF/HEIF presentation timing against container declarations.
 *
 * Returns: TRUE if the track timing is supported; FALSE if invalid or unsupported.
 */
G_GNUC_INTERNAL
gboolean sequence_verify_track_timing(const char *path, uint32_t track_id, uint32_t movie_scale,
                                      uint32_t track_scale, uint64_t movie_duration, GArray *frames,
                                      gboolean *uses_libheif);

#endif
