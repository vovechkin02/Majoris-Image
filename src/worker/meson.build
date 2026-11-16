gexiv = dependency('gexiv2')
native_vala_flags = [
  '--vapidir=' + meson.project_source_root() / 'vapi',
  '--pkg=image-native',
  '--pkg=image-vips',
]
worker_sources = files(
  'main.vala',
  'image-inspector.vala',
  'metadata-reader.vala',
  'float-ops.vala',
  'tone-mapper.vala',
  'image-processor.vala',
  'tone-operations.vala',
  'color-operations.vala',
  'export-engine.vala',
)

# The private archive contains the Vala entry point. link_whole below pulls it
# and the worker-only contract into the executable; the archive is never installed.
worker_code = static_library(
  'majoris-image-worker-code',
  worker_sources,
  worker_contract,
  dependencies: [gio, json, gexiv],
  vala_args: native_vala_flags,
  include_directories: include_directories('../../native'),
)
worker = executable(
  'majoris-image-worker',
  link_whole: worker_code,
  dependencies: [native_api, gexiv],
  install: true,
  install_dir: get_option('libexecdir'),
)
