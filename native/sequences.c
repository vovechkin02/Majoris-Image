#include "bridge.h"
#include <math.h>
#ifdef MAJORIS_AVIF_SEQUENCE
#include <avif/avif.h>
#endif
#ifdef MAJORIS_HEIF_SEQUENCE
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <libheif/heif_security.h>
#include <stdio.h>
#endif

#define FRAME_LIMIT 10000
#define PIXEL_LIMIT 16777216

#if defined(MAJORIS_AVIF_SEQUENCE) || defined(MAJORIS_HEIF_SEQUENCE)
static VipsImage *make_image(const guint8 *data, int width, int height, size_t stride,
                             int depth, int bands, GError **error)
{
    if (!data || width < 1 || height < 1 || (guint64)width * height > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Sequence frame exceeds pixel budget");
        return NULL;
    }
    gboolean wide = depth > 8;
    size_t row = (size_t)width * bands * (wide ? 2 : 1);
    if (stride < row) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid sequence frame stride");
        return NULL;
    }
    guint8 *pixels = g_try_malloc_n(row, height);
    if (!pixels) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate sequence frame");
        return NULL;
    }
    for (int y = 0; y < height; y++)
        memcpy(pixels + y * row, data + y * stride, row);

    VipsImage *image = vips_image_new_from_memory(pixels, row * height, width, height,
                                                   bands, wide ? VIPS_FORMAT_USHORT : VIPS_FORMAT_UCHAR);
    if (!image) {
        g_free(pixels);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
        return NULL;
    }
    g_object_set_data_full(G_OBJECT(image), "sequence-pixels", pixels, g_free);
    image->Type = wide ? VIPS_INTERPRETATION_RGB16 : VIPS_INTERPRETATION_sRGB;
    vips_image_set_int(image, "majoris-image-bit-depth", depth);
    return image;
}

static gboolean set_colour(VipsImage *image, const void *profile, size_t size,
                            int primaries, int transfer, GError **error)
{
    gboolean hdr = transfer == 16 || transfer == 18;
    vips_image_set_int(image, "majoris-image-hdr", hdr);
    if (size > 4 * 1024 * 1024) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Sequence ICC profile exceeds size budget");
        return FALSE;
    }
    if (profile && size && !hdr) {
        vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, profile, size);
        return TRUE;
    }
    if (primaries == 2 || primaries == 0) primaries = 1;
    if (transfer == 2 || transfer == 0) transfer = 13;
    if ((primaries != 1 && primaries != 9 && primaries != 12) ||
        (transfer != 1 && transfer != 6 && transfer != 8 && transfer != 13 &&
         transfer != 14 && transfer != 15 && !hdr)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Sequence colour profile uses unsupported primaries or transfer");
        return FALSE;
    }
    vips_image_set_int(image, "majoris-image-primaries", primaries);
    vips_image_set_int(image, "majoris-image-transfer", transfer);
    return TRUE;
}

static void set_timing(VipsImage *image, const char *loader, int count, int loops,
                       double timestamp, double duration)
{
    vips_image_set_string(image, "vips-loader", loader);
    vips_image_set_int(image, "n-pages", count);
    vips_image_set_int(image, "loop", loops);
    vips_image_set_double(image, "majoris-image-frame-pts", timestamp);
    vips_image_set_double(image, "majoris-image-frame-seconds", duration);
}
#endif

#ifdef MAJORIS_AVIF_SEQUENCE
static VipsImage *load_avif(const char *path, int frame, GError **error)
{
    avifDecoder *decoder = avifDecoderCreate();
    if (!decoder) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate AVIF decoder");
        return NULL;
    }
    VipsImage *image = NULL;
    avifRGBImage rgb = {0};
    decoder->maxThreads = 2;
    decoder->imageSizeLimit = PIXEL_LIMIT;
    decoder->imageDimensionLimit = 16384;
    decoder->imageCountLimit = FRAME_LIMIT;
    decoder->ignoreExif = AVIF_TRUE;
    decoder->ignoreXMP = AVIF_TRUE;
    avifResult result = avifDecoderSetSource(decoder, AVIF_DECODER_SOURCE_TRACKS);
    if (result == AVIF_RESULT_OK) result = avifDecoderSetIOFile(decoder, path);
    if (result == AVIF_RESULT_OK) result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) goto failed;
    if (frame < 0 || frame >= decoder->imageCount) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "AVIF frame index is outside the sequence");
        goto done;
    }
    result = avifDecoderNthImage(decoder, (uint32_t)frame);
    if (result != AVIF_RESULT_OK) goto failed;
    avifImage *src = decoder->image;
    avifRGBImageSetDefaults(&rgb, src);
    gboolean alpha = src->alphaPlane != NULL;
    rgb.format = alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.depth = src->depth > 8 ? 16 : 8;
    result = avifRGBImageAllocatePixels(&rgb);
    if (result == AVIF_RESULT_OK) result = avifImageYUVToRGB(src, &rgb);
    if (result != AVIF_RESULT_OK) goto failed;
    image = make_image(rgb.pixels, src->width, src->height, rgb.rowBytes,
                        src->depth, alpha ? 4 : 3, error);
    if (image && !set_colour(image, src->icc.data, src->icc.size,
                              src->colorPrimaries, src->transferCharacteristics, error))
        g_clear_object(&image);
    if (image) {
        int repeats = decoder->repetitionCount;
        int loops = repeats == AVIF_REPETITION_COUNT_INFINITE ? 0 : repeats < 0 ? 1 : MIN(repeats, G_MAXINT - 1) + 1;
        set_timing(image, "libavif-sequence", decoder->imageCount, loops,
                   decoder->imageTiming.pts, decoder->imageTiming.duration);
    }
    goto done;

failed:
    g_set_error(error, G_IO_ERROR,
                result == AVIF_RESULT_NO_CODEC_AVAILABLE ? G_IO_ERROR_NOT_SUPPORTED : G_IO_ERROR_INVALID_DATA,
                "AVIF sequence: %s", avifResultToString(result));
done:
    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return image;
}
#endif

#ifdef MAJORIS_HEIF_SEQUENCE
// libheif exposes the repeat classification, but its C API does not
// expose composition offsets or edit playback rates. Check only those
// declarations on the chosen track; libheif still owns timeline decoding.
typedef struct {
    uint64_t payload;
    uint64_t end;
    uint32_t type;
} SequenceBox;

static gboolean read_at(FILE *file, uint64_t offset, void *data, size_t length)
{
    return offset <= G_MAXINT64 && fseeko(file, (off_t)offset, SEEK_SET) == 0 &&
           fread(data, 1, length, file) == length;
}

static int next_box(FILE *file, uint64_t end, uint64_t *offset, SequenceBox *box, unsigned *budget)
{
    if (*offset == end) return 0;
    uint32_t header[2];
    if (*offset > end || end - *offset < sizeof header || !*budget ||
        !read_at(file, *offset, header, sizeof header)) return -1;
    (*budget)--;
    uint64_t size = GUINT32_FROM_BE(header[0]);
    uint64_t header_size = sizeof header;
    if (size == 1) {
        uint64_t extended;
        header_size += sizeof extended;
        if (end - *offset < header_size ||
            !read_at(file, *offset + sizeof header, &extended, sizeof extended)) return -1;
        size = GUINT64_FROM_BE(extended);
    } else if (size == 0) {
        size = end - *offset;
    }
    if (size < header_size || size > end - *offset) return -1;
    box->payload = *offset + header_size;
    box->end = *offset + size;
    box->type = GUINT32_FROM_BE(header[1]);
    *offset = box->end;
    return 1;
}

static int find_box(FILE *file, SequenceBox parent, uint32_t type, SequenceBox *found, unsigned *budget)
{
    for (uint64_t offset = parent.payload;;) {
        int status = next_box(file, parent.end, &offset, found, budget);
        if (status <= 0 || found->type == type) return status;
    }
}

typedef struct {
    int sample; // -1 is the transparent interval specified by an empty edit.
    uint64_t start;
    uint64_t duration;
} PresentationFrame;

static gboolean build_edit_frames(FILE *file, SequenceBox list, SequenceBox table,
                                  uint32_t movie_scale, uint32_t track_scale, uint64_t movie_duration, GArray *frames,
                                  gboolean *uses_libheif, unsigned *budget)
{
    uint32_t header[2];
    if (list.end - list.payload < sizeof header ||
        !read_at(file, list.payload, header, sizeof header)) return FALSE;
    uint32_t fullbox = GUINT32_FROM_BE(header[0]);
    uint32_t edits = GUINT32_FROM_BE(header[1]);
    unsigned version = fullbox >> 24;
    unsigned stride = version ? 20 : 12;
    if (version > 1 || edits < 1 || edits > 64 ||
        list.end - list.payload != sizeof header + edits * stride) return FALSE;
    unsigned flags = fullbox & 0xffffff;
    if (flags & ~1u) return FALSE;
    uint64_t period = 0;
    if (flags == 1) {
        uint32_t rate, duration32;
        if (edits != 1 || !read_at(file, list.end - sizeof rate, &rate, sizeof rate) ||
            GUINT32_FROM_BE(rate) != 0x10000) return FALSE;
        if (version == 1) {
            if (!read_at(file, list.payload + sizeof header, &period, sizeof period)) return FALSE;
            period = GUINT64_FROM_BE(period);
        } else {
            if (!read_at(file, list.payload + sizeof header, &duration32, sizeof duration32)) return FALSE;
            period = GUINT32_FROM_BE(duration32);
        }
        if (!period) return FALSE;
        if (movie_duration == G_MAXUINT32 || movie_duration == G_MAXUINT64 ||
            movie_duration % period == 0) {
            *uses_libheif = TRUE;
            return TRUE;
        }
        // A finite whole-media repeat may end in the middle of a cycle.
        uint64_t cycles = movie_duration / period + 1;
        if (cycles > FRAME_LIMIT) return FALSE;
        edits = cycles;
    }

    SequenceBox timing;
    if (!movie_scale || !track_scale ||
        find_box(file, table, heif_fourcc('s','t','t','s'), &timing, budget) != 1 ||
        timing.end - timing.payload < sizeof header ||
        !read_at(file, timing.payload, header, sizeof header)) return FALSE;
    uint32_t runs = GUINT32_FROM_BE(header[1]);
    if (GUINT32_FROM_BE(header[0]) || !runs || runs > FRAME_LIMIT ||
        timing.end - timing.payload != sizeof header + 8u * runs) return FALSE;

    g_autofree uint64_t *starts = g_new0(uint64_t, FRAME_LIMIT + 1);
    unsigned samples = 0;
    for (unsigned i = 0; i < runs; i++) {
        uint32_t fields[2];
        if (!read_at(file, timing.payload + sizeof header + i * sizeof fields, fields, sizeof fields))
            return FALSE;
        uint32_t count = GUINT32_FROM_BE(fields[0]), duration = GUINT32_FROM_BE(fields[1]);
        if (!count || count > FRAME_LIMIT - samples || !duration ||
            duration > (uint64_t)86400 * track_scale) return FALSE;
        for (unsigned n = 0; n < count; n++, samples++)
            starts[samples + 1] = starts[samples] + duration;
    }

    uint64_t cursor = 0, movie_cursor = 0;
    for (unsigned i = 0; i < edits; i++) {
        uint8_t fields[20];
        if (!read_at(file, list.payload + sizeof header + (period ? 0 : i) * stride, fields, stride)) return FALSE;
        uint32_t rate;
        memcpy(&rate, fields + stride - sizeof rate, sizeof rate);
        if (GUINT32_FROM_BE(rate) != 0x10000) return FALSE;
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
            if (start == G_MAXUINT32) start = G_MAXUINT64;
        }
        if (period) {
            // Only expand a repeat of the complete stored timeline.
            if (start != 0 || movie_scale != track_scale || period != starts[samples]) return FALSE;
            length = MIN(period, movie_duration - movie_cursor);
            movie_cursor += length;
        }
        if (length == 0 || length / movie_scale > 86400) return FALSE;
        uint64_t fraction = (length % movie_scale) * track_scale;
        if (fraction % movie_scale) return FALSE;
        length = (length / movie_scale) * track_scale + fraction / movie_scale;
        if (!length || length > (uint64_t)86400 * track_scale ||
            cursor + length > (uint64_t)86400000 * track_scale) return FALSE;
        if (start == G_MAXUINT64) {
            PresentationFrame blank = {-1, cursor, length};
            if (frames->len >= FRAME_LIMIT) return FALSE;
            g_array_append_val(frames, blank);
        } else {
            if (start > starts[samples] || length > starts[samples] - start) return FALSE;
            uint64_t end = start + length;
            for (unsigned n = 0; n < samples && starts[n] < end; n++) {
                uint64_t first = MAX(start, starts[n]);
                uint64_t last = MIN(end, starts[n + 1]);
                if (first >= last) continue;
                PresentationFrame frame = {(int)n, cursor + (first - start), last - first};
                if (frames->len >= FRAME_LIMIT) return FALSE;
                g_array_append_val(frames, frame);
            }
        }
        cursor += length;
    }
    return frames->len > 0;
}

static gboolean verify_timing_boxes(FILE *file, SequenceBox track, unsigned *budget,
                                     uint32_t movie_scale, uint32_t track_scale, uint64_t movie_duration,
                                     GArray *frames, gboolean *uses_libheif)
{
    SequenceBox edit, list, media, info, table, offsets;
    if (find_box(file, track, heif_fourcc('m','d','i','a'), &media, budget) != 1 ||
        find_box(file, media, heif_fourcc('m','i','n','f'), &info, budget) != 1 ||
        find_box(file, info, heif_fourcc('s','t','b','l'), &table, budget) != 1) return FALSE;
    int status = find_box(file, table, heif_fourcc('c','t','t','s'), &offsets, budget);
    if (status < 0) return FALSE;
    if (status > 0) {
        uint32_t header[2];
        uint64_t length = offsets.end - offsets.payload;
        if (length < sizeof header || !read_at(file, offsets.payload, header, sizeof header)) return FALSE;
        uint32_t entries = GUINT32_FROM_BE(header[1]);
        if (entries > FRAME_LIMIT || length != sizeof header + 8u * entries) return FALSE;
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t offset;
            if (!read_at(file, offsets.payload + 12 + 8u * i, &offset, sizeof offset) || offset != 0) return FALSE;
        }
    }
    status = find_box(file, track, heif_fourcc('e','d','t','s'), &edit, budget);
    if (status <= 0) return status == 0;
    return find_box(file, edit, heif_fourcc('e','l','s','t'), &list, budget) == 1 &&
           build_edit_frames(file, list, table, movie_scale, track_scale, movie_duration, frames, uses_libheif, budget);
}

static gboolean verify_track_timing(const char *path, uint32_t track_id,
                                      uint32_t movie_scale, uint32_t track_scale, uint64_t movie_duration,
                                      GArray *frames, gboolean *uses_libheif)
{
    FILE *file = fopen(path, "rb");
    if (!file) return FALSE;
    gboolean supported = FALSE;
    unsigned budget = 16384;
    if (fseeko(file, 0, SEEK_END) != 0) goto done;
    off_t size = ftello(file);
    if (size < 0) goto done;
    SequenceBox root = {0, (uint64_t)size, 0}, movie, track, header;
    if (find_box(file, root, heif_fourcc('m','o','o','v'), &movie, &budget) != 1) goto done;
    for (uint64_t offset = movie.payload; next_box(file, movie.end, &offset, &track, &budget) == 1;) {
        if (track.type != heif_fourcc('t','r','a','k')) continue;
        if (find_box(file, track, heif_fourcc('t','k','h','d'), &header, &budget) != 1) goto done;
        uint8_t version;
        uint32_t id;
        if (header.end - header.payload < 16 || !read_at(file, header.payload, &version, 1)) goto done;
        unsigned id_offset = version == 1 ? 20 : 12;
        if (version > 1 || header.end - header.payload < id_offset + sizeof id ||
            !read_at(file, header.payload + id_offset, &id, sizeof id)) goto done;
        if (GUINT32_FROM_BE(id) != track_id) continue;
        supported = verify_timing_boxes(file, track, &budget, movie_scale, track_scale, movie_duration, frames, uses_libheif);
        break;
    }
done:
    fclose(file);
    return supported;
}

static heif_track *presentation_track(heif_context *context)
{
    uint32_t ids[64];
    int count = heif_context_number_of_sequence_tracks(context);
    if (count < 1 || count > G_N_ELEMENTS(ids)) return NULL;

    heif_context_get_track_ids(context, ids);
    heif_track *chosen = NULL;
    int priority = 0;
    for (int i = 0; i < count; i++) {
        heif_track *track = heif_context_get_track(context, ids[i]);
        if (!track) continue;
        heif_track_type handler = heif_track_get_track_handler_type(track);
        int rank = handler == heif_track_type_image_sequence ? 2 : handler == heif_track_type_video ? 1 : 0;
        uint32_t entry = heif_track_get_sample_entry_type_of_first_cluster(track);
        if ((entry == heif_fourcc('h','v','c','1') || entry == heif_fourcc('h','e','v','1')) &&
            !heif_have_decoder_for_format(heif_compression_HEVC)) rank = 0;
        if (entry == heif_fourcc('a','v','0','1') && !heif_have_decoder_for_format(heif_compression_AV1)) rank = 0;
        if (rank && (rank > priority || (rank == priority && ids[i] < heif_track_get_id(chosen)))) {
            heif_track_release(chosen);
            chosen = track;
            priority = rank;
        } else {
            heif_track_release(track);
        }
    }
    return chosen;
}

static VipsImage *load_heif(const char *path, int frame, int known_count, GError **error)
{
    g_autoptr(GArray) frames = g_array_new(FALSE, FALSE, sizeof(PresentationFrame));
    heif_context *context = heif_context_alloc();
    heif_track *decode_track = NULL;
    heif_decoding_options *options = NULL;
    heif_image *decoded = NULL;
    VipsImage *image = NULL;
    gpointer profile = NULL;
    heif_color_profile_nclx *colour = NULL;
    if (!context) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF decoder");
        goto done;
    }
    heif_security_limits limits = *heif_context_get_security_limits(context);
    limits.max_image_size_pixels = PIXEL_LIMIT;
    limits.max_color_profile_size = 4 * 1024 * 1024;
    limits.max_sequence_frames = FRAME_LIMIT;
    struct heif_error result = heif_context_set_security_limits(context, &limits);
    if (result.code != heif_error_Ok) goto failed;
    result = heif_context_read_from_file(context, path, NULL);
    if (result.code != heif_error_Ok) goto failed;
    decode_track = presentation_track(context);
    if (!decode_track) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "HEIF container has no visual track");
        goto done;
    }
    uint32_t timescale = heif_track_get_timescale(decode_track);
    uint16_t width, height;
    result = heif_track_get_image_resolution(decode_track, &width, &height);
    if (result.code != heif_error_Ok) goto failed;
    if (!timescale || !width || !height || (guint64)width * height > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "HEIF track exceeds geometry or timing budget");
        goto done;
    }

    options = heif_decoding_options_alloc();
    if (!options) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF options");
        goto done;
    }
    uint32_t repetitions = heif_track_get_number_of_repetitions(decode_track);
    uint64_t movie_duration = heif_context_get_sequence_duration(context);
    uint32_t movie_timescale = heif_context_get_sequence_timescale(context);
    gboolean uses_libheif = FALSE;
    if (!verify_track_timing(path, heif_track_get_id(decode_track), movie_timescale, timescale, movie_duration,
                              frames, &uses_libheif) || (repetitions == 0 && !frames->len)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Unsupported HEIF presentation edit list or composition timing");
        goto done;
    }
    PresentationFrame edited = {0};
    if (frames->len) {
        if (frame < 0 || (unsigned)frame >= frames->len) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "HEIF frame index is outside the sequence");
            goto done;
        }
        edited = g_array_index(frames, PresentationFrame, frame);
        repetitions = 1;
    }
    int sample_index = frames->len ? MAX(0, edited.sample) : frame;
    gboolean infinite = repetitions == heif_sequence_track_number_of_repetitions_infinite &&
                        (movie_duration == G_MAXUINT32 || movie_duration == G_MAXUINT64);
    if (!infinite && repetitions > G_MAXINT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid HEIF repetition count");
        goto done;
    }
    // Only factor out whole-track repetition explicitly recognized by libheif.
    options->ignore_sequence_editlist = frames->len || (uses_libheif && repetitions > 1);
    gboolean alpha = heif_track_has_alpha_channel(decode_track);
    int count = 0;
    uint64_t elapsed = 0, timestamp = 0;
    // Consume frames in the order emitted by libheif, with one timeline per playback cycle.
    for (;;) {
        heif_image *next = NULL;
        result = heif_track_decode_next_image(decode_track, &next, heif_colorspace_RGB,
                                               heif_chroma_444, options);
        if (result.code == heif_error_End_of_sequence) break;
        if (result.code != heif_error_Ok) goto failed;
        uint32_t duration = heif_image_get_duration(next);
        if (duration == 0 || (double)duration / timescale > 86400 ||
            (double)(elapsed + duration) / timescale > 86400000) {
            heif_image_release(next);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid HEIF frame timing");
            goto done;
        }
        if (count == sample_index) {
            decoded = next;
            timestamp = elapsed;
        } else {
            heif_image_release(next);
        }
        elapsed += duration;
        count++;
        if (frames->len && decoded) { count = frames->len; break; }
        if (known_count > frame && decoded) { count = known_count; break; }
        if (count > FRAME_LIMIT) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "HEIF track exceeds frame budget");
            goto done;
        }
    }
    if (!known_count && !infinite && repetitions > 1 &&
        (timescale != movie_timescale || elapsed > G_MAXUINT64 / repetitions ||
         elapsed * repetitions != movie_duration)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unsupported partial HEIF presentation repetition");
        goto done;
    }
    if (!decoded) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "HEIF frame index is outside the sequence");
        goto done;
    }
    const enum heif_channel channels[] = {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha};
    int depth = heif_image_get_bits_per_pixel_range(decoded, heif_channel_R);
    int w = heif_image_get_width(decoded, heif_channel_R), h = heif_image_get_height(decoded, heif_channel_R);
    if (depth < 8 || depth > 16 || w < 1 || h < 1 || (guint64)w * h > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unsupported HEIF frame layout");
        goto done;
    }
    gboolean blank = frames->len && edited.sample < 0;
    int bands = alpha || blank ? 4 : 3, component = depth > 8 ? 2 : 1;
    size_t row = (size_t)w * bands * component;
    guint8 *packed = g_try_malloc_n(row, h);
    if (!packed) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF plane buffer");
        goto done;
    }
    if (blank) memset(packed, 0, row * h);
    for (int c = 0; !blank && c < bands; c++) {
        int stride = 0;
        int plane_depth = heif_image_get_bits_per_pixel_range(decoded, channels[c]);
        const guint8 *plane = heif_image_get_plane_readonly(decoded, channels[c], &stride);
        int plane_component = plane_depth > 8 ? 2 : 1;
        if (!plane || plane_depth < 8 || plane_depth > 16 || stride < w * plane_component ||
            heif_image_get_width(decoded, channels[c]) != w || heif_image_get_height(decoded, channels[c]) != h) {
            g_free(packed);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Missing or invalid HEIF colour plane");
            goto done;
        }
        unsigned max = (1u << plane_depth) - 1;
        unsigned output_max = component == 1 ? 255 : 65535;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const guint8 *src = plane + (size_t)y * stride + x * plane_component;
                guint8 *dst = packed + (size_t)y * row + (x * bands + c) * component;
                guint16 value = *src;
                if (plane_component == 2) memcpy(&value, src, 2);
                value = (guint16)((MIN(value, max) * output_max + max / 2) / max);
                if (component == 1) *dst = (guint8)value;
                else memcpy(dst, &value, 2);
            }
        }
    }
    if (alpha && heif_image_is_premultiplied_alpha(decoded)) {
        unsigned max = component == 1 ? 255 : 65535;
        for (guint64 p = 0; p < (guint64)w * h; p++) {
            guint8 *pixel = packed + p * 4 * component;
            guint16 opacity = pixel[3 * component];
            if (component == 2) memcpy(&opacity, pixel + 3 * component, 2);
            for (int c = 0; c < 3; c++) {
                guint8 *sample = pixel + c * component;
                guint16 value = *sample;
                if (component == 2) memcpy(&value, sample, 2);
                value = opacity ? (guint16)MIN(max, (value * max + opacity / 2) / opacity) : 0;
                if (component == 1) *sample = (guint8)value;
                else memcpy(sample, &value, 2);
            }
        }
    }
    image = make_image(packed, w, h, row, depth, bands, error);
    g_free(packed);
    if (!image) goto done;
    size_t size = heif_image_get_raw_color_profile_size(decoded);
    if (size && size <= 4 * 1024 * 1024) {
        profile = g_malloc(size);
        result = heif_image_get_raw_color_profile(decoded, profile);
        if (result.code != heif_error_Ok) { g_clear_object(&image); goto failed; }
    }
    heif_image_get_nclx_color_profile(decoded, &colour);
    if (!set_colour(image, profile, size, colour ? colour->color_primaries : 1,
                     colour ? colour->transfer_characteristics : 13, error)) {
        g_clear_object(&image);
        goto done;
    }
    int loops = infinite ? 0 : (int)repetitions;
    set_timing(image, "libheif-sequence", count, loops,
               (double)(frames->len ? edited.start : timestamp) / timescale,
               (double)(frames->len ? edited.duration : heif_image_get_duration(decoded)) / timescale);
    goto done;

failed:
    g_set_error(error, G_IO_ERROR, result.code == heif_error_Unsupported_feature ?
                G_IO_ERROR_NOT_SUPPORTED : G_IO_ERROR_INVALID_DATA, "HEIF sequence: %s", result.message);
done:
    g_free(profile);
    if (colour) heif_nclx_color_profile_free(colour);
    if (decoded) heif_image_release(decoded);
    if (options) heif_decoding_options_free(options);
    if (decode_track) heif_track_release(decode_track);
    if (context) heif_context_free(context);
    return image;
}
#endif

VipsImage *majoris_codec_sequence(const char *path, int frame, gboolean avif, int known_count, GError **error)
{
#ifdef MAJORIS_AVIF_SEQUENCE
    if (avif) return load_avif(path, frame, error);
#endif
#ifdef MAJORIS_HEIF_SEQUENCE
    return load_heif(path, frame, known_count, error);
#else
    (void)path; (void)frame; (void)avif; (void)known_count; (void)error;
    return NULL;
#endif
}
