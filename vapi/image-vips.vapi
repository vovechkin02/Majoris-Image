[CCode (cheader_filename = "vips/vips.h")]
namespace Vips {
    [CCode (cname = "VipsOperationRelational", cprefix = "VIPS_OPERATION_RELATIONAL_")]
    public enum Relation { EQUAL, NOTEQ, LESS, LESSEQ, MORE, MOREEQ }
    [CCode (cname = "VipsOperationMath", cprefix = "VIPS_OPERATION_MATH_")]
    public enum MathOperation { EXP }
    [CCode (cname = "vips_init")] public int init (string name);
    [CCode (cname = "vips_error_buffer")] public unowned string error_buffer ();
    [CCode (cname = "vips_cache_set_max_mem")] public void cache_set_max_mem (size_t memory);
    [CCode (cname = "vips_cache_set_max")] public void cache_set_max (int count);
    [CCode (cname = "vips_concurrency_set")] public void concurrency_set (int count);
    [CCode (cname = "VipsImage", type_id = "vips_image_get_type ()")]
    public class Image : GLib.Object {
        [CCode (cname = "vips_image_new_from_file")] public static Image? from_file (string path, ...);
        [CCode (cname = "vips_image_write_to_file")] public int write_to_file (string path, ...);
        [CCode (cname = "vips_image_get_width")] public int width ();
        [CCode (cname = "vips_image_get_height")] public int height ();
        [CCode (cname = "vips_image_get_fields", array_length = false, array_null_terminated = true)] public string[] fields ();
        [CCode (cname = "vips_image_get_as_string")] public int get_as_string (string name, out string value);
        [CCode (cname = "vips_image_remove")] public bool remove (string name);
        [CCode (cname = "vips_image_get_typeof")] public GLib.Type type_of (string name);
        [CCode (cname = "vips_image_hasalpha")] public bool has_alpha ();
        [CCode (cname = "vips_image_get_bands")] public int bands ();
        [CCode (cname = "vips_image_get_format")] public int format ();
        [CCode (cname = "vips_image_set_string")] public void set_string (string name, string value);
        [CCode (cname = "vips_image_set_int")] public void set_int (string name, int value);
        [CCode (cname = "vips_image_get_double")] public int get_double (string name, out double value);
        [CCode (cname = "vips_image_get_int")] public int get_int (string name, out int value);
        [CCode (cname = "vips_image_get_string")] public int get_string (string name, out unowned string value);
        [CCode (cname = "vips_image_new_matrix_from_array")] public static Image matrix (int width, int height, double[] values);
        [CCode (cname = "vips_image_copy_memory")] public Image copy_memory ();
    }
    [CCode (cname = "vips_autorot")] public int autorot (Image input, out Image output, ...);
    [CCode (cname = "vips_thumbnail_image")] public int thumbnail (Image input, out Image output, int width, ...);
    [CCode (cname = "vips_rot")] public int rot (Image input, out Image output, int angle, ...);
    [CCode (cname = "vips_flip")] public int flip (Image input, out Image output, int direction, ...);
    [CCode (cname = "vips_extract_area")] public int crop (Image input, out Image output, int x, int y, int width, int height, ...);
    [CCode (cname = "vips_linear1")] public int linear (Image input, out Image output, double a, double b, ...);
    [CCode (cname = "vips_colourspace")] public int colourspace (Image input, out Image output, int space, ...);
    [CCode (cname = "vips_resize")] public int resize (Image input, out Image output, double scale, ...);
    [CCode (cname = "vips_premultiply")] public int premultiply (Image input, out Image output, ...);
    [CCode (cname = "vips_unpremultiply")] public int unpremultiply (Image input, out Image output, ...);
    [CCode (cname = "vips_copy")] public int copy (Image input, out Image output, ...);
    [CCode (cname = "vips_xyz")] public int coordinates (out Image output, int width, int height, ...);
    [CCode (cname = "vips_black")] public int black (out Image output, int width, int height, ...);
    [CCode (cname = "vips_avg")] public int average (Image input, out double value, ...);
    [CCode (cname = "vips_max")] public int maximum (Image input, out double value, ...);
    [CCode (cname = "vips_math")] public int math (Image input, out Image output, MathOperation operation, ...);
    [CCode (cname = "vips_getpoint")] public int point (Image input, [CCode (array_length_pos = 2.1)] out double[] values, int x, int y, ...);
    [CCode (cname = "vips_similarity")] public int similarity (Image input, out Image output, ...);
    [CCode (cname = "vips_sharpen")] public int sharpen (Image input, out Image output, ...);
    [CCode (cname = "vips_icc_transform")] public int icc_transform (Image input, out Image output, string profile, ...);
    [CCode (cname = "vips_icc_import")] public int icc_import (Image input, out Image output, ...);
    [CCode (cname = "vips_extract_band")] public int extract_band (Image input, out Image output, int band, ...);
    [CCode (cname = "vips_bandjoin2")] public int bandjoin (Image first, Image second, out Image output, ...);
    [CCode (cname = "vips_add")] public int add (Image first, Image second, out Image output, ...);
    [CCode (cname = "vips_subtract")] public int subtract (Image first, Image second, out Image output, ...);
    [CCode (cname = "vips_multiply")] public int multiply (Image first, Image second, out Image output, ...);
    [CCode (cname = "vips_divide")] public int divide (Image first, Image second, out Image output, ...);
    [CCode (cname = "vips_abs")] public int absolute (Image input, out Image output, ...);
    [CCode (cname = "vips_recomb")] public int recomb (Image input, out Image output, Image matrix, ...);
    [CCode (cname = "vips_gaussblur")] public int blur (Image input, out Image output, double sigma, ...);
    [CCode (cname = "vips_relational_const1")] public int relational (Image input, out Image output, Relation relation, double value, ...);
    [CCode (cname = "vips_ifthenelse")] public int choose (Image condition, Image yes, Image no, out Image output, ...);
    [CCode (cname = "vips_icc_export")] public int icc_export (Image input, out Image output, ...);
    [CCode (cname = "vips_cast")] public int cast (Image input, out Image output, int format, ...);
}
