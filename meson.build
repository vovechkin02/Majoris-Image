project(
  'majoris-image',
  ['c', 'cpp', 'vala'],
  version: '2.0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.4',
  default_options: ['c_std=gnu11', 'cpp_std=c++17', 'warning_level=1'],
)

pkg = import('pkgconfig')
c_compiler = meson.get_compiler('c')
add_project_arguments('-DGETTEXT_PACKAGE="majoris-image"', language: 'c')

gio = dependency('gio-2.0', version: '>=2.76')
gee = dependency('gee-0.8')
json = dependency('json-glib-1.0')
math = c_compiler.find_library('m')

glycin = dependency('glycin-1', required: get_option('glycin'))
gir_compiler = find_program(
  'gi-compile-repository',
  'g-ir-compiler',
  required: get_option('introspection'),
)

config = configuration_data()
config.set_quoted(
  'WORKER_PATH',
  get_option('prefix') / get_option('libexecdir') / 'majoris-image-worker',
)
configure_file(output: 'config.h', configuration: config)

subdir('src/core')
subdir('native')
subdir('src/worker')
if get_option('gtk')
  subdir('src/gtk')
  subdir('examples')
endif
subdir('src/thumbnail')
subdir('tests')

summary(
  {
    'GTK4': get_option('gtk'),
    'Glycin': glycin.found(),
    'Ultra HDR': uhdr.found(),
    'Ultra HDR HEIF and AVIF containers': ultra_containers,
    'AVIF timed sequences': avif_sequence.found(),
    'HEIF timed sequences': heif_sequence,
    'Introspection typelibs': gir_compiler.found(),
    'Tests': get_option('tests'),
    'Extended GTK tests': get_option('tests') and get_option('gtk') and get_option('extended_tests'),
    'libFuzzer harness': get_option('tests') and get_option('libfuzzer'),
  },
  section: 'Majoris Image features',
  bool_yn: true,
)
