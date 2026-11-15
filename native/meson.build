native_deps = [
  gio, json, math,
  dependency('vips', version: '>=8.14'),
  dependency('libraw'), dependency('lcms2'), dependency('libheif', version: '>=1.15'),
  dependency('OpenEXR', version: '>=3.1'), dependency('libjxl', version: '>=0.10'),
  dependency('librsvg-2.0', version: '>=2.54'),
]
uhdr = dependency('libuhdr', version: '>=2.0.2', required: get_option('ultrahdr'))
native_sources = files('bridge.c', 'raw.c', 'color.c', 'profiles.c', 'codec-catalog.c',
  'exr.cpp', 'exr-source.cpp', 'jxl.c', 'svg.c', 'metadata.c', 'pixels.c', 'sequences.c')
if uhdr.found()
  ultra_containers = meson.get_compiler('c').compiles('''
    #include <ultrahdr_api.h>
    int main(void) { return UHDR_CODEC_HEIF == UHDR_CODEC_AVIF; }
  ''', dependencies: uhdr, name: 'Majoris libuhdr HEIF and AVIF containers')
  native_sources += files('ultra.c')
  native_deps += [uhdr]
else
  native_sources += files('ultra-unavailable.c')
endif
native_c_args = []
avif_sequence = dependency('libavif', version: '>=1.1', required: get_option('sequence'))
heif_sequence = false
if not get_option('sequence').disabled()
  heif_sequence = meson.get_compiler('c').links('''
    #include <libheif/heif.h>
    #include <libheif/heif_sequences.h>
    int main(void) {
      heif_decoding_options *options = heif_decoding_options_alloc();
      options->ignore_sequence_editlist = 1;
      return heif_track_get_number_of_repetitions(0);
    }
  ''', dependencies: dependency('libheif'), name: 'HEIF visual-track decoding and loop control')
endif
if avif_sequence.found()
  native_deps += avif_sequence
  native_c_args += '-DIMAGE_CODEC_AVIF_SEQUENCE=1'
endif
if heif_sequence
  native_c_args += '-DIMAGE_CODEC_HEIF_SEQUENCE=1'
endif
if uhdr.found()
  native_c_args += ['-DIMAGE_CODEC_UHDR_AVAILABLE=1']
endif
if uhdr.found() and ultra_containers
  native_c_args += ['-DIMAGE_CODEC_UHDR_CONTAINERS=1']
endif
native_bridge = static_library('majoris-image-codecs', native_sources,
  include_directories: include_directories('..'), dependencies: native_deps, c_args: native_c_args)
native_api = declare_dependency(link_with: native_bridge, dependencies: native_deps,
  include_directories: include_directories('.'))
