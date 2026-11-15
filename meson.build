project('majoris-image', ['c', 'cpp', 'vala'], version: '1.1.1',
  license: 'GPL-3.0-or-later', meson_version: '>=1.4',
  default_options: ['c_std=gnu11', 'cpp_std=c++17', 'warning_level=1'])

gio = dependency('gio-2.0', version: '>=2.76')
gee = dependency('gee-0.8')
json = dependency('json-glib-1.0')
math = meson.get_compiler('c').find_library('m')
pkg = import('pkgconfig')
gir_compiler = find_program('gi-compile-repository', 'g-ir-compiler', required: get_option('introspection'))
add_project_arguments('-DGETTEXT_PACKAGE="majoris-image"', language: 'c')

config = configuration_data()
config.set_quoted('WORKER_PATH', get_option('prefix') / get_option('libexecdir') / 'majoris-image-worker')
configure_file(output: 'config.h', configuration: config)

glycin = dependency('glycin-1', required: get_option('glycin'))
subdir('src/core')
subdir('native')
subdir('src/worker')
if get_option('gtk')
  subdir('src/gtk')
  subdir('examples')
endif
subdir('src/thumbnail')
subdir('tests')
