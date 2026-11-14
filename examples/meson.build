gnome = import('gnome')
resources = gnome.compile_resources('demo-resources', 'demo.gresource.xml', source_dir: '.')
demo = executable('majoris-image-demo', 'demo.vala', resources, dependencies: gtk_api)
builder_example = executable('majoris-image-builder-example', 'builder.c', dependencies: gtk_api)
