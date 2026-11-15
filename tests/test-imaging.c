#include "bridge.h"
#include <glib/gstdio.h>
#include <math.h>
#include <libheif/heif.h>

static VipsImage *constant_image(float value, gboolean alpha) {
    int bands = alpha ? 4 : 3;
    float *data = g_new(float, 64*32*bands);
    for (int p=0;p<64*32;p++) {
        data[p*bands] = data[p*bands+1] = data[p*bands+2] = value;
        if (alpha) data[p*bands+3] = .375;
    }
    VipsImage *image = vips_image_new_from_memory_copy(data,64*32*bands*sizeof(float),64,32,bands,VIPS_FORMAT_FLOAT);
    g_free(data); image->Type = VIPS_INTERPRETATION_scRGB;
    return image;
}
static double sample(VipsImage *image, int band) {
    double *pixel = NULL; int count = 0;
    g_assert_cmpint(vips_getpoint(image,&pixel,&count,16,16,NULL),==,0);
    g_assert_cmpint(count,>,band); double value=pixel[band]; g_free(pixel); return value;
}
static void linear_reference(void) {
    GError *error = NULL; VipsImage *input=constant_image(.5,TRUE);
    VipsImage *linear=image_codec_to_linear(input,&error); g_assert_no_error(error);
    g_assert_cmpfloat_with_epsilon(sample(linear,0),.5,1e-6);
    g_assert_cmpfloat_with_epsilon(sample(linear,3),.375,1e-6);
    VipsImage *encoded=NULL; g_assert_cmpint(vips_colourspace(linear,&encoded,VIPS_INTERPRETATION_sRGB,NULL),==,0);
    g_assert_cmpfloat_with_epsilon(sample(encoded,3),.375*255,1);
    g_object_unref(encoded);
    g_object_unref(linear); g_object_unref(input);
}
static void pq_reference(void) {
    GError *error=NULL; VipsImage *input=constant_image(.5080784215,FALSE);
    vips_image_set_int(input,"majoris-image-transfer",16); vips_image_set_int(input,"majoris-image-primaries",9);
    VipsImage *linear=image_codec_to_linear(input,&error); g_assert_no_error(error);
    g_assert_cmpfloat_with_epsilon(sample(linear,0),100.0/203.0,.0001);
    g_object_unref(linear); g_object_unref(input);
}
static void rec2020_primary_reference(void)
{
    const float red[] = {1, 0, 0, .5f};
    g_autoptr(VipsImage) source = vips_image_new_from_memory_copy(red, sizeof red, 1, 1, 4, VIPS_FORMAT_FLOAT);
    source->Type = VIPS_INTERPRETATION_scRGB;
    vips_image_set_int(source, "majoris-image-transfer", 8);
    vips_image_set_int(source, "majoris-image-primaries", 9);
    g_autoptr(GError) error = NULL;
    g_autoptr(VipsImage) converted = image_codec_to_linear(source, &error);
    g_assert_no_error(error);
    g_assert_nonnull(converted);
    size_t length;
    g_autofree float *pixel = vips_image_write_to_memory(converted, &length);
    g_assert_cmpuint(length, ==, sizeof red);
    // Linear BT.2020 red expressed in linear sRGB, including negative values.
    g_assert_cmpfloat_with_epsilon(pixel[0], 1.66049, .0001);
    g_assert_cmpfloat_with_epsilon(pixel[1], -.12455, .0001);
    g_assert_cmpfloat_with_epsilon(pixel[2], -.01815, .0001);
    g_assert_cmpfloat_with_epsilon(pixel[3], .5, 1e-6);
}

static void float_buffer_boundaries(void)
{
    const float values[] = {NAN, INFINITY, -INFINITY, NAN, -2, 4, .25f, -1, 4, -2, .5f, 2};
    const float expected[] = {0, 0, 0, 1, -2, 4, .25f, 0, 4, -2, .5f, 1};
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = g_dir_make_tmp("imagekit-float-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *path = g_build_filename(dir, "float.buffer", NULL);
    g_autoptr(VipsImage) source = vips_image_new_from_memory_copy(values, sizeof values, 3, 1, 4, VIPS_FORMAT_FLOAT);
    g_assert_true(image_codec_write_pixels(source, path, &error));
    g_assert_no_error(error);
    g_autoptr(VipsImage) decoded = image_codec_read_pixels(path, &error);
    g_assert_no_error(error);
    g_assert_nonnull(decoded);
    size_t length;
    g_autofree float *pixels = vips_image_write_to_memory(decoded, &length);
    g_assert_cmpmem(pixels, length, expected, sizeof expected);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
}

static void profile_reference(void) {
    GError *error=NULL; VipsImage *input=constant_image(.18,FALSE);
    const char *profiles[]={"srgb","display-p3","adobe-rgb","prophoto"};
    for (int p=0;p<4;p++) {
        VipsImage *encoded=image_codec_convert_profile(input,profiles[p],16,&error); g_assert_no_error(error); g_assert_nonnull(encoded);
        VipsImage *restored=image_codec_to_linear(encoded,&error); g_assert_no_error(error);
        g_assert_cmpfloat_with_epsilon(sample(restored,0),.18,.002);
        g_object_unref(restored); g_object_unref(encoded);
    }
    g_object_unref(input);
}
static void profile_alpha_reference(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(VipsImage) input = constant_image(.18, TRUE);
    for (int depth = 8; depth <= 16; depth += 8) {
        double max = depth == 16 ? 65535 : 255;
        g_autoptr(VipsImage) encoded = image_codec_convert_profile(input, "srgb", depth, &error);
        g_assert_no_error(error);
        g_assert_nonnull(encoded);
        g_assert_cmpfloat_with_epsilon(sample(encoded, 3), round(.375 * max), .001);
        g_autoptr(VipsImage) decoded = image_codec_to_linear(encoded, &error);
        g_assert_no_error(error);
        g_assert_nonnull(decoded);
        g_assert_cmpfloat_with_epsilon(sample(decoded, 3), round(.375 * max) / max, 1e-6);
        g_assert_cmpfloat_with_epsilon(sample(decoded, 0), .18, .005);
    }
}

static void p3_primary_reference(void) {
    GError *error=NULL; VipsImage *gray=constant_image(.18,FALSE);
    VipsImage *profiled=image_codec_convert_profile(gray,"display-p3",16,&error); g_assert_no_error(error);
    guint8 red[64*32*3]; for (int p=0;p<64*32;p++) { red[p*3]=255; red[p*3+1]=red[p*3+2]=0; }
    VipsImage *input=vips_image_new_from_memory_copy(red,sizeof(red),64,32,3,VIPS_FORMAT_UCHAR); input->Type=VIPS_INTERPRETATION_sRGB;
    const void *profile; size_t size; g_assert_cmpint(vips_image_get_blob(profiled,VIPS_META_ICC_NAME,&profile,&size),==,0);
    vips_image_set_blob_copy(input,VIPS_META_ICC_NAME,profile,size);
    VipsImage *linear=image_codec_to_linear(input,&error); g_assert_no_error(error);
    g_test_message("P3 red → linear sRGB: %.6f %.6f %.6f",sample(linear,0),sample(linear,1),sample(linear,2));
    g_assert_cmpfloat_with_epsilon(sample(linear,0),1.224745,.005);
    g_assert_cmpfloat_with_epsilon(sample(linear,1),-.042058,.005);
    g_assert_cmpfloat_with_epsilon(sample(linear,2),-.019642,.005);
    g_object_unref(linear); g_object_unref(input); g_object_unref(profiled); g_object_unref(gray);
}
static void exr_roundtrip(void) {
    GError *error=NULL; char *dir=g_dir_make_tmp("majoris-image-exr-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"hdr.exr",NULL);
    VipsImage *input=constant_image(4.1234567f,TRUE);
    for (int depth=16;depth<=32;depth+=16) {
        gboolean written = image_codec_exr_encode(input,path,depth,&error); g_assert_no_error(error); g_assert_true(written);
        VipsImage *loaded=image_codec_exr_image(path,0,&error); g_assert_no_error(error); g_assert_nonnull(loaded);
        g_assert_cmpfloat_with_epsilon(sample(loaded,0),4.1234567f,depth == 16 ? .003 : .000001);
        g_assert_cmpfloat_with_epsilon(sample(loaded,3),.375,.001);
        g_object_unref(loaded); g_unlink(path);
    }
    g_object_unref(input); g_rmdir(dir); g_free(path); g_free(dir);
}
static void hdr_tiff_roundtrip(void) {
    GError *error=NULL; char *dir=g_dir_make_tmp("majoris-image-tiff-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"hdr.tiff",NULL); VipsImage *input=constant_image(4,TRUE);
    image_codec_tag_linear(input); g_assert_cmpint(vips_image_write_to_file(input,path,"compression",VIPS_FOREIGN_TIFF_COMPRESSION_DEFLATE,NULL),==,0);
    VipsImage *loaded=vips_image_new_from_file(path,NULL); g_assert_nonnull(loaded);
    VipsImage *linear=image_codec_to_linear(loaded,&error); g_assert_no_error(error);
    g_assert_cmpfloat_with_epsilon(sample(linear,0),4,.01); g_assert_cmpfloat_with_epsilon(sample(linear,3),.375,.001);
    g_object_unref(linear); g_object_unref(loaded); g_object_unref(input);
    g_unlink(path); g_rmdir(dir); g_free(path); g_free(dir);
}
static void hdr_jxl_roundtrip(void) {
    GError *error=NULL; char *dir=g_dir_make_tmp("majoris-image-jxl-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"hdr.jxl",NULL); VipsImage *input=constant_image(4,TRUE);
    gboolean written=image_codec_jxl_encode(input,path,32,100,TRUE,&error); g_assert_no_error(error); g_assert_true(written);
    VipsImage *loaded=vips_image_new_from_file(path,NULL); g_assert_nonnull(loaded);
    g_assert_cmpint(loaded->BandFmt,==,VIPS_FORMAT_FLOAT);
    g_assert_cmpfloat_with_epsilon(sample(loaded,0),4,.001);
    g_assert_cmpfloat_with_epsilon(sample(loaded,3),.375,.001);
    g_object_unref(loaded); g_object_unref(input);
    g_unlink(path); g_rmdir(dir); g_free(path); g_free(dir);
}
static void svg_region_scaling(void) {
    GError *error=NULL; char *dir=g_dir_make_tmp("majoris-image-svg-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"fine.svg",NULL);
    g_assert_true(g_file_set_contents(path,"<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'><rect x='40' width='.1' height='100' fill='red'/></svg>",-1,&error)); g_assert_no_error(error);
    VipsImage *tile=image_codec_svg_region(path,.4,0,.001,.002,100,100,1024,&error); g_assert_no_error(error); g_assert_nonnull(tile);
    g_assert_cmpint(tile->Xsize,==,512); g_assert_cmpint(tile->Ysize,==,1024);
    g_assert_cmpfloat_with_epsilon(sample(tile,0),255,1); g_assert_cmpfloat_with_epsilon(sample(tile,1),0,1); g_assert_cmpfloat(sample(tile,3),==,255);
    g_object_unref(tile); g_unlink(path); g_rmdir(dir); g_free(path); g_free(dir);
}
static void heif_transfer_reference(gconstpointer parameter) {
    int transfer=GPOINTER_TO_INT(parameter); GError *error=NULL;
    char *dir=g_dir_make_tmp("majoris-image-heif-transfer-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"transfer.avif",NULL);
    struct heif_context *context=heif_context_alloc(); struct heif_encoder *encoder=NULL;
    if (heif_context_get_encoder_for_format(context,heif_compression_AV1,&encoder).code != heif_error_Ok) {
        g_test_skip("No HEIF AV1 encoder plugin");
        heif_context_free(context); g_rmdir(dir); g_free(path); g_free(dir);
        return;
    }
    struct heif_image *image=NULL; g_assert_cmpint(heif_image_create(64,32,heif_colorspace_RGB,heif_chroma_interleaved_RRGGBB_LE,&image).code,==,heif_error_Ok);
    g_assert_cmpint(heif_image_add_plane(image,heif_channel_interleaved,64,32,10).code,==,heif_error_Ok);
    int stride; guint8 *plane=heif_image_get_plane(image,heif_channel_interleaved,&stride);
    int code=transfer==16 ? 520 : 1023;
    for (int y=0;y<32;y++) for (int x=0;x<64*3;x++) ((guint16 *)(plane+y*stride))[x]=code;
    struct heif_color_profile_nclx *profile=heif_nclx_color_profile_alloc(); profile->color_primaries=heif_color_primaries_ITU_R_BT_2020_2_and_2100_0;
    profile->transfer_characteristics=(enum heif_transfer_characteristics)transfer; profile->matrix_coefficients=heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance; profile->full_range_flag=1;
    g_assert_cmpint(heif_image_set_nclx_color_profile(image,profile).code,==,heif_error_Ok);
    heif_encoder_set_lossless(encoder,1); struct heif_encoding_options *options=heif_encoding_options_alloc(); options->output_nclx_profile=profile;
    struct heif_image_handle *handle=NULL; g_assert_cmpint(heif_context_encode_image(context,image,encoder,options,&handle).code,==,heif_error_Ok);
    g_assert_cmpint(heif_context_write_to_file(context,path).code,==,heif_error_Ok);
    heif_image_handle_release(handle); heif_encoding_options_free(options); heif_nclx_color_profile_free(profile); heif_image_release(image); heif_encoder_release(encoder); heif_context_free(context);
    VipsImage *loaded=vips_image_new_from_file(path,NULL); g_assert_nonnull(loaded); image_codec_annotate_heif(loaded,path);
    VipsImage *linear=image_codec_to_linear(loaded,&error); g_assert_no_error(error);
    double expected=transfer==16 ? image_codec_pq_to_linear(code/1023.0)/203.0 : 1000.0/203.0;
    g_test_message("transfer %d: encoded %.3f → linear %.6f (expected %.6f)",transfer,sample(loaded,0),sample(linear,0),expected);
    g_assert_cmpfloat_with_epsilon(sample(linear,0),expected,.02);
    g_object_unref(linear); g_object_unref(loaded); g_unlink(path); g_rmdir(dir); g_free(path); g_free(dir);
}
static void gainmap_roundtrip(gconstpointer data) {
    int format=GPOINTER_TO_INT(data); GError *error=NULL;
    char *dir=g_dir_make_tmp("majoris-image-gainmap-XXXXXX",&error); g_assert_no_error(error);
    char *path=g_build_filename(dir,"gainmap",NULL); VipsImage *input=constant_image(4,FALSE);
    gboolean written = image_codec_ultra_encode(input,path,format,95,&error); g_assert_no_error(error); g_assert_true(written);
    g_assert_true(image_codec_ultra_probe(path));
    VipsImage *hdr=image_codec_ultra_decode(path,FALSE,&error); g_assert_no_error(error); g_assert_nonnull(hdr);
    VipsImage *sdr=image_codec_ultra_decode(path,TRUE,&error); g_assert_no_error(error); g_assert_nonnull(sdr);
    g_test_message("gain-map format %d: HDR %.5f SDR %.5f",format,sample(hdr,0),sample(sdr,0));
    g_assert_cmpfloat_with_epsilon(sample(hdr,0),4,.4);
    g_assert_cmpfloat(sample(sdr,0),<=,1.001);
    g_object_unref(sdr); g_object_unref(hdr); g_object_unref(input);
    g_unlink(path); g_rmdir(dir); g_free(path); g_free(dir);
}
int main(int argc,char **argv) {
    g_test_init(&argc,&argv,NULL); VIPS_INIT("majoris-image-imaging-tests"); vips_cache_set_max(0); vips_concurrency_set(2);
    g_test_add_func("/imaging/linear-alpha",linear_reference);
    g_test_add_func("/imaging/pq-reference-white",pq_reference);
    g_test_add_func("/imaging/rec2020-primary-reference", rec2020_primary_reference);
    g_test_add_func("/imaging/float-buffer-boundaries", float_buffer_boundaries);
    g_test_add_func("/imaging/icc-roundtrip",profile_reference);
    g_test_add_func("/imaging/p3-primary-reference",p3_primary_reference);
    g_test_add_func("/imaging/icc-alpha-roundtrip", profile_alpha_reference);
    g_test_add_func("/imaging/exr-roundtrip",exr_roundtrip);
    g_test_add_func("/imaging/hdr-tiff-roundtrip",hdr_tiff_roundtrip);
    g_test_add_func("/imaging/hdr-jxl-roundtrip",hdr_jxl_roundtrip);
    g_test_add_func("/imaging/svg-source-region-scaling",svg_region_scaling);
    g_test_add_data_func("/imaging/pq-avif-10bit",GINT_TO_POINTER(16),heif_transfer_reference);
    g_test_add_data_func("/imaging/hlg-avif-10bit",GINT_TO_POINTER(18),heif_transfer_reference);
#ifdef IMAGE_CODEC_UHDR_AVAILABLE
    g_test_add_data_func("/imaging/gainmap-jpeg",GINT_TO_POINTER(0),gainmap_roundtrip);
#ifdef IMAGE_CODEC_UHDR_CONTAINERS
    g_test_add_data_func("/imaging/gainmap-heif",GINT_TO_POINTER(1),gainmap_roundtrip);
    g_test_add_data_func("/imaging/gainmap-avif",GINT_TO_POINTER(2),gainmap_roundtrip);
#endif
#endif
    return g_test_run();
}
