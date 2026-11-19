#include "sequence-timeline.h"

#include <stdio.h>
#include <string.h>

#include <libheif/heif.h>

// libheif exposes the repeat classification, but its C API does not
// expose composition offsets or edit playback rates. Check only those
// declarations on the chosen track; libheif still owns timeline decoding.
/** Parsed ISO BMFF box boundaries used to validate nested timing metadata. */
typedef struct {
    uint64_t payload;
    uint64_t end;
    uint32_t type;
} SequenceBox;

/** Reads exactly the requested bounded byte range from a source file. */
static gboolean read_at(FILE *file, uint64_t offset, void *data, size_t length)
{
    return offset <= G_MAXINT64 && fseeko(file, (off_t)offset, SEEK_SET) == 0 &&
           fread(data, 1, length, file) == length;
}

/** Parses the next bounded ISO BMFF box without crossing the parent box boundary. */
static int next_box(FILE *file, uint64_t end, uint64_t *offset, SequenceBox *box, unsigned *budget)
{
    if (*offset == end) {
        return 0;
    }
    uint32_t header[2];
    if (*offset > end || end - *offset < sizeof header || !*budget ||
        !read_at(file, *offset, header, sizeof header)) {
        return -1;
    }
    (*budget)--;
    uint64_t size = GUINT32_FROM_BE(header[0]);
    uint64_t header_size = sizeof header;
    if (size == 1) {
        uint64_t extended;
        header_size += sizeof extended;
        if (end - *offset < header_size ||
            !read_at(file, *offset + sizeof header, &extended, sizeof extended)) {
            return -1;
        }
        size = GUINT64_FROM_BE(extended);
    } else if (size == 0) {
        size = end - *offset;
    }
    if (size < header_size || size > end - *offset) {
        return -1;
    }
    box->payload = *offset + header_size;
    box->end = *offset + size;
    box->type = GUINT32_FROM_BE(header[1]);
    *offset = box->end;
    return 1;
}

/** Searches a bounded ISO BMFF box hierarchy for the requested child type. */
static int find_box(FILE *file, SequenceBox parent, uint32_t type, SequenceBox *found,
                    unsigned *budget)
{
    for (uint64_t offset = parent.payload;;) {
        int status = next_box(file, parent.end, &offset, found, budget);
        if (status <= 0 || found->type == type) {
            return status;
        }
    }
}

/** Converts edit-list entries to a validated sequence of presentation intervals. */
static gboolean build_edit_frames(FILE *file, SequenceBox list, SequenceBox table,
                                  uint32_t movie_scale, uint32_t track_scale,
                                  uint64_t movie_duration, GArray *frames, gboolean *uses_libheif,
                                  unsigned *budget)
{
    uint32_t header[2];
    if (list.end - list.payload < sizeof header ||
        !read_at(file, list.payload, header, sizeof header)) {
        return FALSE;
    }
    uint32_t fullbox = GUINT32_FROM_BE(header[0]);
    uint32_t edits = GUINT32_FROM_BE(header[1]);
    unsigned version = fullbox >> 24;
    unsigned stride = version ? 20 : 12;
    if (version > 1 || edits < 1 || edits > 64 ||
        list.end - list.payload != sizeof header + edits * stride) {
        return FALSE;
    }
    unsigned flags = fullbox & 0xffffff;
    if (flags & ~1u) {
        return FALSE;
    }
    uint64_t period = 0;
    if (flags == 1) {
        uint32_t rate, duration32;
        if (edits != 1 || !read_at(file, list.end - sizeof rate, &rate, sizeof rate) ||
            GUINT32_FROM_BE(rate) != 0x10000) {
            return FALSE;
        }
        if (version == 1) {
            if (!read_at(file, list.payload + sizeof header, &period, sizeof period)) {
                return FALSE;
            }
            period = GUINT64_FROM_BE(period);
        } else {
            if (!read_at(file, list.payload + sizeof header, &duration32, sizeof duration32)) {
                return FALSE;
            }
            period = GUINT32_FROM_BE(duration32);
        }
        if (!period) {
            return FALSE;
        }
        if (movie_duration == G_MAXUINT32 || movie_duration == G_MAXUINT64 ||
            movie_duration % period == 0) {
            *uses_libheif = TRUE;
            return TRUE;
        }
        // A finite whole-media repeat may end in the middle of a cycle.
        uint64_t cycles = movie_duration / period + 1;
        if (cycles > SEQUENCE_FRAME_LIMIT) {
            return FALSE;
        }
        edits = cycles;
    }

    SequenceBox timing;
    if (!movie_scale || !track_scale ||
        find_box(file, table, heif_fourcc('s', 't', 't', 's'), &timing, budget) != 1 ||
        timing.end - timing.payload < sizeof header ||
        !read_at(file, timing.payload, header, sizeof header)) {
        return FALSE;
    }
    uint32_t runs = GUINT32_FROM_BE(header[1]);
    if (GUINT32_FROM_BE(header[0]) || !runs || runs > SEQUENCE_FRAME_LIMIT ||
        timing.end - timing.payload != sizeof header + 8u * runs) {
        return FALSE;
    }

    g_autofree uint64_t *starts = g_new0(uint64_t, SEQUENCE_FRAME_LIMIT + 1);
    unsigned samples = 0;
    for (unsigned i = 0; i < runs; i++) {
        uint32_t fields[2];
        if (!read_at(file, timing.payload + sizeof header + i * sizeof fields, fields,
                     sizeof fields)) {
            return FALSE;
        }
        uint32_t count = GUINT32_FROM_BE(fields[0]), duration = GUINT32_FROM_BE(fields[1]);
        if (!count || count > SEQUENCE_FRAME_LIMIT - samples || !duration ||
            duration > (uint64_t)86400 * track_scale) {
            return FALSE;
        }
        for (unsigned n = 0; n < count; n++, samples++) {
            starts[samples + 1] = starts[samples] + duration;
        }
    }

    uint64_t cursor = 0, movie_cursor = 0;
    for (unsigned i = 0; i < edits; i++) {
        uint8_t fields[20];
        if (!read_at(file, list.payload + sizeof header + (period ? 0 : i) * stride, fields,
                     stride)) {
            return FALSE;
        }
        uint32_t rate;
        memcpy(&rate, fields + stride - sizeof rate, sizeof rate);
        if (GUINT32_FROM_BE(rate) != 0x10000) {
            return FALSE;
        }
        uint64_t length, start;
        if (version == 1) {
            memcpy(&length, fields, 8);
            memcpy(&start, fields + 8, 8);
            length = GUINT64_FROM_BE(length);
            start = GUINT64_FROM_BE(start);
        } else {
            uint32_t length32, start32;
            memcpy(&length32, fields, 4);
            memcpy(&start32, fields + 4, 4);
            length = GUINT32_FROM_BE(length32);
            start = GUINT32_FROM_BE(start32);
            if (start == G_MAXUINT32) {
                start = G_MAXUINT64;
            }
        }
        if (period) {
            // Only expand a repeat of the complete stored timeline.
            if (start != 0 || movie_scale != track_scale || period != starts[samples]) {
                return FALSE;
            }
            length = MIN(period, movie_duration - movie_cursor);
            movie_cursor += length;
        }
        if (length == 0 || length / movie_scale > 86400) {
            return FALSE;
        }
        uint64_t fraction = (length % movie_scale) * track_scale;
        if (fraction % movie_scale) {
            return FALSE;
        }
        length = (length / movie_scale) * track_scale + fraction / movie_scale;
        if (!length || length > (uint64_t)86400 * track_scale ||
            cursor + length > (uint64_t)86400000 * track_scale) {
            return FALSE;
        }
        if (start == G_MAXUINT64) {
            PresentationFrame blank = {-1, cursor, length};
            if (frames->len >= SEQUENCE_FRAME_LIMIT) {
                return FALSE;
            }
            g_array_append_val(frames, blank);
        } else {
            if (start > starts[samples] || length > starts[samples] - start) {
                return FALSE;
            }
            uint64_t end = start + length;
            for (unsigned n = 0; n < samples && starts[n] < end; n++) {
                uint64_t first = MAX(start, starts[n]);
                uint64_t last = MIN(end, starts[n + 1]);
                if (first >= last) {
                    continue;
                }
                PresentationFrame frame = {(int)n, cursor + (first - start), last - first};
                if (frames->len >= SEQUENCE_FRAME_LIMIT) {
                    return FALSE;
                }
                g_array_append_val(frames, frame);
            }
        }
        cursor += length;
    }
    return frames->len > 0;
}

/** Validates the chosen track timing and edit-list interpretation. */
static gboolean verify_timing_boxes(FILE *file, SequenceBox track, unsigned *budget,
                                    uint32_t movie_scale, uint32_t track_scale,
                                    uint64_t movie_duration, GArray *frames, gboolean *uses_libheif)
{
    SequenceBox edit, list, media, info, table, offsets;
    if (find_box(file, track, heif_fourcc('m', 'd', 'i', 'a'), &media, budget) != 1 ||
        find_box(file, media, heif_fourcc('m', 'i', 'n', 'f'), &info, budget) != 1 ||
        find_box(file, info, heif_fourcc('s', 't', 'b', 'l'), &table, budget) != 1) {
        return FALSE;
    }
    int status = find_box(file, table, heif_fourcc('c', 't', 't', 's'), &offsets, budget);
    if (status < 0) {
        return FALSE;
    }
    if (status > 0) {
        uint32_t header[2];
        uint64_t length = offsets.end - offsets.payload;
        if (length < sizeof header || !read_at(file, offsets.payload, header, sizeof header)) {
            return FALSE;
        }
        uint32_t entries = GUINT32_FROM_BE(header[1]);
        if (entries > SEQUENCE_FRAME_LIMIT || length != sizeof header + 8u * entries) {
            return FALSE;
        }
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t offset;
            if (!read_at(file, offsets.payload + 12 + 8u * i, &offset, sizeof offset) ||
                offset != 0) {
                return FALSE;
            }
        }
    }
    status = find_box(file, track, heif_fourcc('e', 'd', 't', 's'), &edit, budget);
    if (status <= 0) {
        return status == 0;
    }
    return find_box(file, edit, heif_fourcc('e', 'l', 's', 't'), &list, budget) == 1 &&
           build_edit_frames(file, list, table, movie_scale, track_scale, movie_duration, frames,
                             uses_libheif, budget);
}

/** Validates AVIF/HEIF presentation timing against container declarations. */
gboolean sequence_verify_track_timing(const char *path, uint32_t track_id, uint32_t movie_scale,
                                      uint32_t track_scale, uint64_t movie_duration, GArray *frames,
                                      gboolean *uses_libheif)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return FALSE;
    }
    gboolean supported = FALSE;
    unsigned budget = 16384;
    if (fseeko(file, 0, SEEK_END) != 0) {
        goto done;
    }
    off_t size = ftello(file);
    if (size < 0) {
        goto done;
    }
    SequenceBox root = {0, (uint64_t)size, 0}, movie, track, header;
    if (find_box(file, root, heif_fourcc('m', 'o', 'o', 'v'), &movie, &budget) != 1) {
        goto done;
    }
    for (uint64_t offset = movie.payload;
         next_box(file, movie.end, &offset, &track, &budget) == 1;) {
        if (track.type != heif_fourcc('t', 'r', 'a', 'k')) {
            continue;
        }
        if (find_box(file, track, heif_fourcc('t', 'k', 'h', 'd'), &header, &budget) != 1) {
            goto done;
        }
        uint8_t version;
        uint32_t id;
        if (header.end - header.payload < 16 || !read_at(file, header.payload, &version, 1)) {
            goto done;
        }
        unsigned id_offset = version == 1 ? 20 : 12;
        if (version > 1 || header.end - header.payload < id_offset + sizeof id ||
            !read_at(file, header.payload + id_offset, &id, sizeof id)) {
            goto done;
        }
        if (GUINT32_FROM_BE(id) != track_id) {
            continue;
        }
        supported = verify_timing_boxes(file, track, &budget, movie_scale, track_scale,
                                        movie_duration, frames, uses_libheif);
        break;
    }
done:
    fclose(file);
    return supported;
}
