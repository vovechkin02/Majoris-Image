gtk = dependency('gtk4', version: '>=4.16')
glycin = dependency('glycin-1', required: get_option('glycin'))
glycin_gtk = dependency('glycin-gtk4-1', required: get_option('glycin'))
gtk_deps = [core_api, gtk, gee, math]
gtk_flags = ['--shared-library=libmajoris-image-gtk4.so.1', '--hide-internal']
if glycin.found() and glycin_gtk.found()
  gtk_deps += [glycin, glycin_gtk]
  gtk_flags += ['--define=HAVE_GLYCIN']
endif

gtk_library = shared_library('majoris-image-gtk4',
  'image-view.vala', 'tile-cache.vala', 'image-texture.c',
  dependencies: gtk_deps, vala_args: gtk_flags, include_directories: include_directories('.'),
  version: meson.project_version(), soversion: '1', install: true,
  vala_header: 'majoris-image-gtk4.h', vala_vapi: 'majoris-image-gtk4-1.vapi', vala_gir: 'MajorisImageGtk-1.0.gir',
  install_dir: [get_option('libdir'), get_option('includedir') / 'majoris-image-gtk4-1', get_option('datadir') / 'vala/vapi', get_option('datadir') / 'gir-1.0'])
if gir_compiler.found()
  custom_target('MajorisImageGtk typelib', output: 'MajorisImageGtk-1.0.typelib',
    command: [gir_compiler, '--includedir=' + core_gir_dir, '-o', '@OUTPUT@',
      meson.current_build_dir() / 'MajorisImageGtk-1.0.gir'],
    depends: [gtk_library, core_typelib], install: true, install_dir: get_option('libdir') / 'girepository-1.0')
endif
gtk_api = declare_dependency(link_with: gtk_library, dependencies: [core_api, gtk],
  include_directories: include_directories('.'))
pkg.generate(gtk_library, filebase: 'majoris-image-gtk4-1', name: 'Majoris Image GTK4',
  description: 'Reusable GTK4 image view', subdirs: 'majoris-image-gtk4-1',
  requires: ['majoris-image-1', 'gtk4'])
install_data('majoris-image-gtk4-1.deps', install_dir: get_option('datadir') / 'vala/vapi')
