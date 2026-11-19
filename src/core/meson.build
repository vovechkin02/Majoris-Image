remote_publication_sources = files('export/non-native-publication.vala', 'operation-profile.vala')
core_sources = files(
  'image-error.vala',
  'image-source.vala',
  'decoding/source-sniff-stream.vala',
  'pixel-buffer.vala',
  'image-metadata.vala',
  'image-document.vala',
  'decoding/decoder-types.vala',
  'decoding/decoder-backend.vala',
  'decoding/decoder-policy.vala',
  'decoding/image-probe.vala',
  'decoding/decoder-registry.vala',
  'decoding/glycin-fallback.vala',
  'decoding/source-precision.vala',
  'export/encoder-registry.vala',
  'export/export-transfer.c',
  'render-options.vala',
  'export/export-options.vala',
  'thumbnail/thumbnail-service.vala',
  'preload-manager.vala',
  'execution/job-scheduler.vala',
  'decoding/decode-options.vala',
  'execution/worker-request.vala',
  'execution/worker-client.vala',
  'execution/worker-output-reader.c',
  'execution/worker-failure.vala',
  'execution/worker-path.c',
  'thumbnail/thumbnail-png.c',
  'capability-contract.c',
) + remote_publication_sources
core_dependencies = [gio, gee, json, math, dependency('zlib')]
# The shared worker contract only needs recipe copying and serialization.
render_options_vala_args = ['--define=RENDER_OPTIONS_DEFAULT_CHECK']
core_vala_args = [
  # Worker-only contract methods remain available to the worker and protocol tests.
  '--define=CORE_WORKER_CLIENT',
  '--shared-library=libmajoris-image.so.2',
  '--hide-internal',
] + render_options_vala_args
if glycin.found()
  core_dependencies += [glycin]
  core_sources += files('decoding/glycin-pixels.c', '../../native/glycin-discovery.c')
  core_vala_args += ['--define=HAVE_GLYCIN']
endif
core_library = shared_library(
  'majoris-image',
  core_sources,
  dependencies: core_dependencies,
  include_directories: include_directories('../..', '../../native', 'execution', 'export'),
  version: meson.project_version(),
  soversion: '2',
  install: true,
  vala_args: core_vala_args,
  vala_header: 'majoris-image.h',
  vala_vapi: 'majoris-image-1.vapi',
  vala_gir: 'ImageKit-2.0.gir',
  install_dir: [
    get_option('libdir'),
    get_option('includedir') / 'majoris-image-1',
    get_option('datadir') / 'vala/vapi',
    get_option('datadir') / 'gir-1.0',
  ],
)
core_gir_dir = meson.current_build_dir()
if gir_compiler.found()
  core_typelib = custom_target(
    'ImageKit typelib',
    output: 'ImageKit-2.0.typelib',
    command: [gir_compiler, '-o', '@OUTPUT@', core_gir_dir / 'ImageKit-2.0.gir'],
    depends: core_library,
    install: true,
    install_dir: get_option('libdir') / 'girepository-1.0',
  )
endif
core_includes = include_directories('.')
core_api = declare_dependency(
  link_with: core_library,
  include_directories: core_includes,
  dependencies: [gio],
)
worker_contract = files(
  'image-error.vala',
  'decoding/decoder-types.vala',
  'decoding/decoder-backend.vala',
  'decoding/image-probe.vala',
  'render-options.vala',
  'export/export-options.vala',
  'decoding/decode-options.vala',
  'execution/worker-request.vala',
  'execution/worker-failure.vala',
)
pkg.generate(
  core_library,
  filebase: 'majoris-image-1',
  name: 'Majoris Image Core',
  description: 'Sandboxed image processing and thumbnails',
  subdirs: 'majoris-image-1',
  requires: ['gio-2.0'],
)
install_data('majoris-image-1.deps', install_dir: get_option('datadir') / 'vala/vapi')
