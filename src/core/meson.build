core_sources = files(
  'image-error.vala', 'image-source.vala', 'image-document.vala',
  'decoder-backend.vala', 'decoder-registry.vala', 'glycin-fallback.vala', 'encoder-registry.vala',
  'render-options.vala', 'export-options.vala', 'thumbnail-service.vala', 'preload-manager.vala',
  'job-scheduler.vala', 'worker-request.vala', 'worker-client.vala', 'worker-failure.vala',
  'worker-path.c', 'thumbnail-png.c',
)
core_dependencies = [gio, gee, json, math, dependency('zlib')]
core_vala_args = ['--shared-library=libmajoris-image.so.1', '--hide-internal']
if glycin.found()
  core_dependencies += [glycin]
  core_sources += files('glycin-pixels.c')
  core_vala_args += ['--define=HAVE_GLYCIN']
endif
core_library = shared_library('majoris-image', core_sources, dependencies: core_dependencies, include_directories: include_directories('../..'),
  version: meson.project_version(), soversion: '1', install: true,
  vala_args: core_vala_args,
  vala_header: 'majoris-image.h', vala_vapi: 'majoris-image-1.vapi', vala_gir: 'MajorisImage-1.0.gir',
  install_dir: [get_option('libdir'), get_option('includedir') / 'majoris-image-1', get_option('datadir') / 'vala/vapi', get_option('datadir') / 'gir-1.0'])
core_gir_dir = meson.current_build_dir()
if gir_compiler.found()
  core_typelib = custom_target('MajorisImage typelib', output: 'MajorisImage-1.0.typelib',
    command: [gir_compiler, '-o', '@OUTPUT@', core_gir_dir / 'MajorisImage-1.0.gir'],
    depends: core_library, install: true, install_dir: get_option('libdir') / 'girepository-1.0')
endif
core_includes = include_directories('.')
core_api = declare_dependency(link_with: core_library, include_directories: core_includes, dependencies: [gio])
worker_contract = files('image-error.vala', 'decoder-backend.vala', 'render-options.vala',
  'export-options.vala', 'worker-request.vala', 'worker-failure.vala')
pkg.generate(core_library, filebase: 'majoris-image-1', name: 'Majoris Image Core',
  description: 'Sandboxed image processing and thumbnails', subdirs: 'majoris-image-1',
  requires: ['gio-2.0'])
install_data('majoris-image-1.deps', install_dir: get_option('datadir') / 'vala/vapi')
