decoder_test = executable('test-decoders', 'test-decoder-selection.vala', dependencies: core_api)
headless = executable('test-headless', 'test-core-api.vala', dependencies: core_api)
test(
  'Headless Core API',
  python,
  suite: 'integration',
  args: isolated + [headless, fixture_dir, large_tiff_fixture],
  depends: [worker, fixtures],
  env: worker_env,
  timeout: 90,
)
test(
  'Decoder selection and composited APNG frames',
  python,
  suite: 'integration',
  args: isolated + [
    decoder_test,
    fallback_qoi_fixture,
    partial_apng_fixture,
    get_option('glycin').enabled() ? 'enabled' : 'optional',
    worker_fault.full_path(),
    fixture_dir / 'avif-sequence.avif',
    avif_sequence.found() ? 'avif' : 'optional',
    heif_sequence ? fixture_dir / 'heic-sequence.heic' : '',
    fixture_dir / 'avif-hdr-sequence.avif',
    thumbnail_registration,
  ],
  depends: [worker, worker_fault, extra_formats, fixtures],
  env: worker_env,
)
test(
  'Desktop thumbnail registration and output',
  python,
  suite: 'integration',
  args: isolated + [
    python.full_path(),
    files('thumbnail_registration.py'),
    thumbnailer,
    thumbnail_registration,
    fixture_dir,
    fixture_build_dir,
    thumbnail_mime_generator,
    meson.project_source_root() / 'src/thumbnail/majoris-image.thumbnailer.in',
    glycin.found() ? 'glycin' : 'native',
  ],
  depends: [worker, thumbnailer, thumbnail_registration, fixtures, extra_formats],
  env: worker_env,
  timeout: 30,
)
if get_option('raw_fixtures') != ''
  raw_samples = executable(
    'test-raw-samples',
    'test-raw-qualification.vala',
    dependencies: [core_api, json],
  )
  test(
    'Real-camera RAW decoding',
    python,
    suite: 'interoperability',
    args: isolated + [
      raw_samples,
      get_option('raw_fixtures'),
      fixture_dir / 'raw-qualification.tsv',
    ],
    depends: worker,
    env: worker_env,
    timeout: 180,
  )
else
  test(
    'Real-camera RAW decoding',
    python,
    suite: 'interoperability',
    args: [
      '-c',
      'print("External RAW fixtures require -Draw_fixtures=/path"); raise SystemExit(77)',
    ],
  )
endif

if get_option('gtk')
  xvfb = find_program('xvfb-run', required: false)
  xtst = dependency('xtst', required: false)
  x11 = dependency('x11', required: false)
  gtk_x11 = dependency('gtk4-x11', required: false)
  if xvfb.found() and xtst.found() and x11.found() and gtk_x11.found()
    interaction = executable(
      'test-widget-interaction',
      'test-widget-interaction.c',
      dependencies: [gtk_api, gtk_x11, xtst, x11],
    )
    foreach scale : ['1', '2']
      input_env = environment({'GDK_BACKEND': 'x11', 'GDK_SCALE': scale, 'GTK_A11Y': 'test'})
      test(
        'GTK input and accessibility at scale ' + scale,
        python,
        suite: 'integration',
        args: isolated + [xvfb.full_path(), '-a', '-s', '-screen 0 1920x1200x24', interaction],
        env: input_env,
        timeout: 30,
      )
    endforeach
  endif
  tile_source = configure_file(
    input: '../../src/gtk/tile-cache.vala',
    output: 'tile-cache-impl.vala',
    copy: true,
  )
  tile_test = executable(
    'test-tile-coverage',
    ['test-tile-coverage.vala', tile_source],
    dependencies: [gtk_api, gee, math],
    include_directories: include_directories('../../src/gtk'),
  )
  test(
    'Viewport tile coverage and cancellation',
    python,
    suite: 'integration',
    args: isolated + [tile_test, large_tiff_fixture, fixture_dir / 'multipart.exr'],
    depends: [worker, fixtures],
    env: worker_env,
    timeout: 45,
  )
  viewer_test = executable('test-image-view', 'test-image-view.vala', dependencies: gtk_api)
  viewer_args = [fixture_dir, still_webp_fixture, animated_gif_fixture, animated_webp_fixture]
  if avif_sequence.found()
    viewer_args += [fixture_dir / 'avif-sequence.avif', fixture_dir / 'avif-hdr-sequence.avif']
  endif
  if heif_sequence
    viewer_args += [fixture_dir / 'heic-sequence.heic']
  endif
  if glycin.found()
    viewer_args += [partial_apng_fixture, wide_apng_fixture]
  endif
  workflow_sequences = []
  if avif_sequence.found()
    workflow_sequences += [fixture_dir / 'avif-sequence.avif']
  endif
  if heif_sequence
    workflow_sequences += [fixture_dir / 'heic-sequence.heic']
  endif
  raw_preview_fixture = ''
  if get_option('raw_fixtures') != '' and fs.is_file(get_option('raw_fixtures') / '350D.CR2')
    raw_preview_fixture = get_option('raw_fixtures') / '350D.CR2'
  endif
  files_args = [
    fixture_dir / 'rgb16.png',
    fixture_dir / 'multipart.exr',
    raw_preview_fixture,
    large_tiff_fixture,
    profiled_png_fixture,
  ] + workflow_sequences
  photos_args = [
    large_tiff_fixture,
    fixture_dir / 'multipart.exr',
    '@TEST_ROOT@/photos-export.png',
    raw_preview_fixture,
    profiled_png_fixture,
    navigation_jpeg_fixture,
  ] + workflow_sequences
  photos_args += [fallback_qoi_fixture]
  display = xvfb.found() ? [xvfb.full_path(), '-a'] : []
  test(
    'Files public preview workflow',
    python,
    suite: 'integration',
    args: isolated + display + [files_preview] + files_args,
    depends: [worker, fixtures, files_preview],
    env: worker_env,
    timeout: 90,
  )
  test(
    'Photos public full-quality workflow',
    python,
    suite: 'integration',
    args: isolated + display + [photos_viewer] + photos_args,
    depends: [worker, fixtures, extra_formats, photos_viewer],
    env: worker_env,
    timeout: 90,
  )
  test(
    'GtkBuilder view loading',
    python,
    suite: 'integration',
    args: isolated + display + [builder_example, fixture_dir / 'rgb16.png'],
    depends: worker,
    env: worker_env,
  )
  installed_viewer = executable('test-public-viewer', installed_viewer_source, dependencies: gtk_api)
  test(
    'GTK viewer readiness and load errors',
    python,
    suite: 'integration',
    args: isolated + display + [
      installed_viewer,
      fixture_dir / 'rgb16.png',
      fixture_dir / 'corrupt.bin',
    ],
    depends: worker,
    env: worker_env,
    timeout: 30,
  )
  test(
    'GTK view lifecycle and navigation',
    python,
    suite: 'integration',
    args: isolated + display + [viewer_test] + viewer_args,
    depends: [worker, fixtures, extra_formats],
    env: worker_env,
    timeout: 45,
  )
  if get_option('wayland_tests')
    wayland_env = environment(
      {
        'MAJORIS_IMAGE_WORKER': worker.full_path(),
        'G_DEBUG': 'fatal-criticals',
        'GDK_BACKEND': 'wayland',
      },
    )
    test(
      'Wayland view lifecycle',
      python,
      suite: 'platform',
      args: isolated + [viewer_test] + viewer_args,
      depends: [worker, fixtures, extra_formats],
      env: wayland_env,
      timeout: 60,
    )
  endif
  if get_option('extended_tests')
    test(
      'GTK lifecycle stress',
      python,
      suite: 'extended',
      args: isolated + ['--repeat', '3'] + display + [viewer_test] + viewer_args,
      depends: [worker, fixtures, extra_formats],
      env: worker_env,
      timeout: 160,
    )
    test(
      'Complete 8K tile coverage',
      python,
      suite: 'extended',
      args: isolated + [tile_test, large_tiff_fixture, fixture_dir / 'multipart.exr', '--complete'],
      depends: [worker, fixtures],
      env: worker_env,
      timeout: 180,
    )
  endif
endif
