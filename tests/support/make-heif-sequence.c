#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>

static void
check (heif_error error)
{
    if (error.code != heif_error_Ok) {
        fprintf (stderr, "%s\n", error.message);
        exit (1);
    }
}

typedef struct {
    heif_encoder *encoder;
    heif_track_options *track_options;
    heif_sequence_encoding_options *encoding_options;
} TrackWriter;

static int
write_track (heif_context *context, heif_track_type type, TrackWriter *writer)
{
    heif_encoder *encoder = NULL;
    if (heif_context_get_encoder_for_format (context, heif_compression_HEVC, &encoder).code
        != heif_error_Ok)
        return 77;
    heif_encoder_set_logging_level (encoder, 0);
    heif_track_options *track_options = heif_track_options_alloc ();
    heif_track_options_set_timescale (track_options, 1000);
    heif_sequence_encoding_options *options = heif_sequence_encoding_options_alloc ();
    options->gop_structure = heif_sequence_gop_structure_intra_only;
    heif_track *track = NULL;
    check (heif_context_add_visual_sequence_track (context, 64, 64, type, track_options, options,
                                                   &track));
    for (unsigned frame = 0; frame < 2; frame++) {
        heif_image *image = NULL;
        check (
            heif_image_create (64, 64, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &image));
        check (heif_image_add_plane (image, heif_channel_interleaved, 64, 64, 8));
        int stride = 0;
        uint8_t *pixels = heif_image_get_plane (image, heif_channel_interleaved, &stride);
        unsigned channel = type == heif_track_type_video ? 2 : frame;
        for (int y = 0; y < 64; y++) {
            memset (pixels + y * stride, 0, 64 * 3);
            for (int x = 0; x < 64; x++)
                pixels[y * stride + x * 3 + channel] = 255;
        }
        heif_image_set_duration (image, frame ? 160 : 70);
        check (heif_track_encode_sequence_image (track, image, encoder, options));
        heif_image_release (image);
    }
    check (heif_track_encode_end_of_sequence (track, encoder));
    heif_track_release (track);
    *writer = (TrackWriter){ encoder, track_options, options };
    return 0;
}

int
main (int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 1;
    heif_context *context = heif_context_alloc ();
    heif_context_set_number_of_sequence_repetitions (context, 2);
    TrackWriter writers[2] = { 0 };
    int status = argc == 3 ? write_track (context, heif_track_type_video, &writers[0]) : 0;
    if (!status)
        status = write_track (context, heif_track_type_image_sequence, &writers[1]);
    if (!status)
        check (heif_context_write_to_file (context, argv[1]));
    heif_context_free (context);
    for (int i = 0; i < 2; i++) {
        if (!writers[i].encoder)
            continue;
        heif_track_options_release (writers[i].track_options);
        heif_sequence_encoding_options_release (writers[i].encoding_options);
        heif_encoder_release (writers[i].encoder);
    }
    return status;
}
