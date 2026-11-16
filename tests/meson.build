if get_option('tests')
  python = find_program('python3')
  fs = import('fs')
  isolated = files('support/run_isolated.py')
  fixture_dir = meson.current_source_dir() / 'fixtures'
  fixture_build_dir = meson.current_build_dir()
  installed_viewer_source = files('release/test-installed-viewer.vala')
  test_zlib = dependency('zlib')

  # Packaging and installed consumers use the generated large.tiff in this directory.
  fixture_maker = executable('make-fixtures', 'support/make-fixtures.c', dependencies: native_api)
  fixtures = custom_target('large and animated fixtures',
    output: ['large.tiff', 'animated.gif', 'animated.webp', 'jpeg2000.jp2',
      'profiled.png', 'profiled.tiff', 'adobe.png', 'gray-profile.png', 'still.webp',
      'navigation.jpg'],
    command: [fixture_maker, '@OUTPUT@'])
  large_tiff_fixture = fixtures[0]
  animated_gif_fixture = fixtures[1]
  animated_webp_fixture = fixtures[2]
  profiled_png_fixture = fixtures[4]
  still_webp_fixture = fixtures[8]
  navigation_jpeg_fixture = fixtures[9]

  extra_formats = custom_target('legacy and APNG fixtures',
    input: 'support/make_formats.py',
    depend_files: files('fixtures/heic-sequence.heic'),
    output: ['fallback.qoi', 'partial.apng',
      'orientation-1.tiff', 'orientation-2.tiff', 'orientation-3.tiff', 'orientation-4.tiff',
      'orientation-5.tiff', 'orientation-6.tiff', 'orientation-7.tiff', 'orientation-8.tiff',
      'fallback.bmp', 'fallback.ico', 'fallback.tga', 'fallback.dds', 'fallback.pgm',
      'invalid-profile.png', 'srgb.png', 'cicp.png',
      'infinite.heic', 'offset.heic', 'invalid-timing.heic', 'rate.heic', 'composition.heic',
      'trimmed.heic', 'linear.qoi', 'partial-repeat.heic', 'wide.apng'],
    command: [python, '@INPUT@', profiled_png_fixture, '@OUTPUT@'])
  fallback_qoi_fixture = extra_formats[0]
  partial_apng_fixture = extra_formats[1]
  wide_apng_fixture = extra_formats[26]


  worker_env = environment({'MAJORIS_IMAGE_WORKER': worker.full_path(),
    'G_DEBUG': 'fatal-criticals'})

  subdir('unit')
  # Integration tests also use the fault-injection executable.
  subdir('security')
  subdir('integration')
  subdir('benchmarks')
endif
