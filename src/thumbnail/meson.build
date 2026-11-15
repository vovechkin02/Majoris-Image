thumbnailer = executable('majoris-image-thumbnailer', 'main.vala', dependencies: core_api, vala_args: ['--pkg=posix'],
  install: true, install_dir: get_option('libexecdir'))
thumbnail_mime_generator = executable('majoris-image-thumbnail-types', 'mime-registration.c', '../../native/glycin-discovery.c', dependencies: native_api,
  c_args: glycin.found() ? ['-DIMAGE_CODEC_GLYCIN_SYSTEM'] : [],
  install: true, install_dir: get_option('libexecdir'))
thumbnail_registration = custom_target('majoris-image.thumbnailer',
  input: 'majoris-image.thumbnailer.in', output: 'majoris-image.thumbnailer',
  command: [thumbnail_mime_generator, '@INPUT@', '@OUTPUT@',
    get_option('prefix') / get_option('libexecdir') / 'majoris-image-thumbnailer', 'baseline'],
  install: true, install_dir: get_option('datadir') / 'thumbnailers', build_by_default: true)
