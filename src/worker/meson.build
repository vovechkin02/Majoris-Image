gexiv = dependency('gexiv2')
native_vala_flags = [
  '--vapidir=' + meson.project_source_root() / 'vapi',
  '--pkg=image-native',
  '--pkg=image-vips',
]
worker_sources = files(
  'color-adjustments.vala',
  'export-engine.vala',
  'float-ops.vala',
  'image-inspector.vala',
  'image-processor.vala',
  'metadata-reader.vala',
  'tone-adjustments.vala',
  'tone-mapper.vala',
  'worker-dispatcher.vala',
)

worker = executable(
  'majoris-image-worker',
  'main.vala',
  worker_sources,
  worker_contract,
  dependencies: [gio, json, gexiv],
  # Link the codec archive without passing its C-only packages to valac.
  link_with: native_bridge,
  include_directories: include_directories('../../native'),
  vala_args: native_vala_flags,
  install: true,
  install_dir: get_option('libexecdir'),
)
