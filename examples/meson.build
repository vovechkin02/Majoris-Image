gnome = import('gnome')
resources = gnome.compile_resources('demo-resources', 'demo.gresource.xml', source_dir: '.')
demo = executable('majoris-image-demo', 'demo.vala', resources, dependencies: gtk_api)
builder_example = executable('majoris-image-builder-example', 'builder.c', dependencies: gtk_api)
files_preview = executable(
  'majoris-image-files-preview',
  'files-preview.vala',
  dependencies: gtk_api,
)
photos_viewer = executable(
  'majoris-image-photos-viewer',
  'photos-viewer.vala',
  dependencies: gtk_api,
)
