thumbnailer = executable('majoris-image-thumbnailer', 'main.vala', dependencies: core_api,
  install: true, install_dir: get_option('libexecdir'))
thumbnail_config = configuration_data()
thumbnail_config.set('THUMBNAILER', get_option('prefix') / get_option('libexecdir') / 'majoris-image-thumbnailer')
configure_file(input: 'majoris-image.thumbnailer.in', output: 'majoris-image.thumbnailer',
  configuration: thumbnail_config, install_dir: get_option('datadir') / 'thumbnailers')
