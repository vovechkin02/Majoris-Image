if get_option('tests')
  python = find_program('python3')
  isolated = files('run-isolated.py')
  exr_tests = executable('test-exr', 'test-exr.cpp', dependencies: native_api)
  test('OpenEXR regions', python, args: isolated + [exr_tests], suite: 'unit')
  color_tests = executable('test-imaging', 'test-imaging.c', dependencies: native_api, c_args: native_c_args)
  test('Imaging references', python, args: isolated + [color_tests], suite: 'unit', timeout: 150)

  png_test = executable('test-thumbnail-png', 'thumbnail-png.c',
    dependencies: [core_api, dependency('vips'), dependency('zlib')])
  test('thumbnail PNG decode and validation', png_test, suite: 'unit', timeout: 15)

  fixture_dir = meson.current_source_dir() / 'fixtures'
  fuzz_native = executable('fuzz-native-smoke', 'fuzz-native.c',
    dependencies: [native_api, core_api, dependency('zlib')])
  test('native parser fuzz smoke', python, suite: 'security',
    args: isolated + [fuzz_native, fixture_dir / 'rgb16.png', fixture_dir / 'heic-sequence.heic',
      fixture_dir / 'avif-sequence.avif'], timeout: 40)
  protocol_smoke = executable('test-protocol-smoke', ['protocol-smoke.vala'] + worker_contract,
    dependencies: [gio, json, math])
  test('worker protocol properties', protocol_smoke, suite: 'security',
    env: {'G_DEBUG': 'fatal-criticals'}, timeout: 15)
  if get_option('libfuzzer')
    executable('fuzz-imagekit', 'fuzz-native.c',
      dependencies: [native_api, core_api, dependency('zlib')],
      c_args: ['-DIMAGEKIT_LIBFUZZER', '-fsanitize=fuzzer'],
      link_args: ['-fsanitize=fuzzer'])
  endif

  fixture_maker = executable('make-fixtures', 'make-fixtures.c', dependencies: native_api)
  fixtures = custom_target('large and animated fixtures',
    output: ['large.tiff', 'animated.gif', 'animated.webp', 'jpeg2000.jp2', 'profiled.png', 'profiled.tiff', 'adobe.png', 'gray-profile.png', 'still.webp', 'navigation.jpg'],
    command: [fixture_maker, '@OUTPUT@'])
  fs = import('fs')
  extra_formats = custom_target('legacy and APNG fixtures',
    input: 'make-formats.py', depend_files: files('fixtures/heic-sequence.heic'), output: ['fallback.qoi', 'partial.apng',
      'orientation-1.tiff', 'orientation-2.tiff', 'orientation-3.tiff', 'orientation-4.tiff',
      'orientation-5.tiff', 'orientation-6.tiff', 'orientation-7.tiff', 'orientation-8.tiff',
      'fallback.bmp', 'fallback.ico', 'fallback.tga', 'fallback.dds', 'fallback.pgm', 'invalid-profile.png', 'srgb.png', 'cicp.png',
      'infinite.heic', 'offset.heic', 'invalid-timing.heic', 'rate.heic', 'composition.heic', 'trimmed.heic', 'linear.qoi', 'partial-repeat.heic', 'wide.apng'],
    command: [python, '@INPUT@', fixtures[4], '@OUTPUT@'])
  decoder_test = executable('test-decoders', 'decoders.vala', dependencies: core_api)

  worker_env = environment({'MAJORIS_IMAGE_WORKER': worker.full_path(),
    'G_DEBUG': 'fatal-criticals'})
  scheduler_source = configure_file(input: '../src/core/job-scheduler.vala',
    output: 'job-scheduler-impl.vala', copy: true)
  scheduler_test = executable('test-preview-scheduler', ['scheduler.vala', scheduler_source],
    dependencies: [gio, gee])
  test('foreground, tile and preload scheduling', scheduler_test, suite: 'unit', timeout: 10)
  benchmark_program = executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
  workloads = ['open=' + (fixture_dir / 'rgb16.png'), 'warm=' + (fixture_dir / 'rgb16.png'),
    'thumbnail=' + (fixture_dir / 'rgb16.png'), 'preload=' + (fixture_dir / 'rgb16.png'),
    'navigate=' + (fixture_dir / 'rgb16.png'), 'icc=' + (meson.current_build_dir() / 'profiled.png'),
    'tile=' + (meson.current_build_dir() / 'large.tiff'), 'tiles=' + (meson.current_build_dir() / 'large.tiff'),
    'hdr-sdr=' + (fixture_dir / 'multipart.exr'), 'animation=' + (meson.current_build_dir() / 'animated.gif')]
  if get_option('raw_fixtures') != ''
    workloads += ['raw-preview=' + (get_option('raw_fixtures') / '350D.CR2'),
      'raw-develop=' + (get_option('raw_fixtures') / '350D.CR2')]
  endif
  benchmark('public API performance', python, suite: 'performance', timeout: 900,
    args: isolated + [python, files('run-benchmarks.py'), '--build', meson.project_build_root(),
      '--worker', worker, '--executable', benchmark_program] + workloads,
    depends: [fixtures, worker])

  headless = executable('test-headless', 'headless.vala', dependencies: core_api)
  test('public headless API', python, suite: 'integration',
      args: isolated + [headless, fixture_dir, fixtures[0]], depends: [worker, fixtures],
    env: worker_env, timeout: 90)

  worker_fault = executable('worker-fault', 'worker-fault.c')
  test('decoder selection and composited APNG frames', python, suite: 'integration',
      args: isolated + [decoder_test, extra_formats[0], extra_formats[1], get_option('glycin').enabled() ? 'enabled' : 'optional', worker_fault.full_path(),
      fixture_dir / 'avif-sequence.avif', avif_sequence.found() ? 'avif' : 'optional',
      heif_sequence ? fixture_dir / 'heic-sequence.heic' : '', fixture_dir / 'avif-hdr-sequence.avif', thumbnail_registration],
    depends: [worker, worker_fault, extra_formats, fixtures], env: worker_env)
  test('desktop thumbnail registration and sample output', python, suite: 'integration',
    args: isolated + [python, files('thumbnail-registration.py'), thumbnailer, thumbnail_registration, fixture_dir, meson.current_build_dir(),
      thumbnail_mime_generator, meson.project_source_root() / 'src/thumbnail/majoris-image.thumbnailer.in',
      glycin.found() ? 'glycin' : 'native'],
    depends: [worker, thumbnailer, thumbnail_registration, fixtures, extra_formats], env: worker_env, timeout: 30)
  publish_gate = shared_module('cache-publish-gate', 'cache-publish-gate.c',
    dependencies: meson.get_compiler('c').find_library('dl'))
  test('cross-process thumbnail publication', python, suite: 'security',
    args: isolated + [python, files('cache-processes.py'), thumbnailer, fixture_dir / 'rgb16.png', publish_gate],
    depends: [worker, thumbnailer, publish_gate], env: worker_env, timeout: 40)
  if get_option('raw_fixtures') != ''
    raw_samples = executable('test-raw-samples', 'raw-samples.vala', dependencies: [core_api, json])
    test('real camera RAW', python, suite: 'interoperability',
      args: isolated + [raw_samples, get_option('raw_fixtures'), fixture_dir / 'raw-qualification.tsv'],
      depends: worker, env: worker_env, timeout: 180)
  else
    test('real camera RAW', python, suite: 'interoperability',
      args: ['-c', 'print("External RAW fixtures require -Draw_fixtures=/path"); raise SystemExit(77)'])
  endif

  worker_security = executable('test-worker-security', 'worker-security.vala', dependencies: core_api)
  test('worker timeout, cancellation, crash and response validation', python, suite: 'security',
      args: isolated + [worker_security, worker_fault.full_path ()], depends: worker_fault, env: worker_env, timeout: 15)

  if get_option('gtk')
    xvfb = find_program('xvfb-run', required: false)
    policy_source = configure_file(input: '../src/gtk/presentation.vala',
      output: 'presentation-policy-impl.vala', copy: true)
    presentation_test = executable('test-presentation-policy',
      ['presentation.vala', policy_source],
      include_directories: include_directories('../src/gtk'), dependencies: gtk_api)
    test('HDR presentation policy', presentation_test, suite: 'unit')
    texture_test = executable('test-image-texture', 'image-texture.c',
      include_directories: include_directories('../src/gtk'), dependencies: gtk_api)
    test('GDK floating-point texture color state', texture_test, suite: 'unit')
    xtst = dependency('xtst', required: false)
    x11 = dependency('x11', required: false)
    gtk_x11 = dependency('gtk4-x11', required: false)
    if xvfb.found() and xtst.found() and x11.found() and gtk_x11.found()
      interaction = executable('test-widget-interaction', 'widget-interaction.c',
        dependencies: [gtk_api, gtk_x11, xtst, x11])
      foreach scale : ['1', '2']
        input_env = environment({'GDK_BACKEND': 'x11', 'GDK_SCALE': scale, 'GTK_A11Y': 'test'})
        test('GTK input and accessibility at scale ' + scale, python, suite: 'integration',
          args: isolated + [xvfb, '-a', '-s', '-screen 0 1920x1200x24', interaction], env: input_env, timeout: 30)
      endforeach
    endif
    tile_source = configure_file(input: '../src/gtk/tile-cache.vala',
      output: 'tile-cache-impl.vala', copy: true)
    tile_test = executable('test-tile-coverage', ['tile-coverage.vala', tile_source],
      dependencies: [gtk_api, gee, math], include_directories: include_directories('../src/gtk'))
    test('wide viewport tile coverage and cancellation', python, suite: 'integration',
      args: isolated + [tile_test, fixtures[0], fixture_dir / 'multipart.exr'], depends: [worker, fixtures], env: worker_env, timeout: 45)
    viewer_test = executable('test-image-view', 'viewer.vala', dependencies: gtk_api)
    viewer_args = [fixture_dir, fixtures[8], fixtures[1], fixtures[2]]
    if avif_sequence.found()
      viewer_args += [fixture_dir / 'avif-sequence.avif', fixture_dir / 'avif-hdr-sequence.avif']
    endif
    if heif_sequence
      viewer_args += [fixture_dir / 'heic-sequence.heic']
    endif
    if glycin.found()
      viewer_args += [extra_formats[1], extra_formats[26]]
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
    files_args = [fixture_dir / 'rgb16.png', fixture_dir / 'multipart.exr',
      raw_preview_fixture, fixtures[0], fixtures[4]] + workflow_sequences
    photos_args = [fixtures[0], fixture_dir / 'multipart.exr', '@TEST_ROOT@/photos-export.png',
      raw_preview_fixture, fixtures[4], fixtures[9]] + workflow_sequences
    photos_args += [extra_formats[0]]
    display = xvfb.found() ? [xvfb, '-a'] : []
    test('Files public preview workflow', python, suite: 'integration',
      args: isolated + display + [files_preview] + files_args,
      depends: [worker, fixtures, files_preview], env: worker_env, timeout: 90)
    test('Photos public full-quality workflow', python, suite: 'integration',
      args: isolated + display + [photos_viewer] + photos_args,
      depends: [worker, fixtures, extra_formats, photos_viewer], env: worker_env, timeout: 90)
    test('public GTK Builder API', python, suite: 'integration',
      args: isolated + display + [builder_example, fixture_dir / 'rgb16.png'],
      depends: worker, env: worker_env)
    installed_viewer = executable('test-public-viewer', 'installed-viewer.vala', dependencies: gtk_api)
    test('public GTK viewer readiness and error', python, suite: 'integration',
      args: isolated + display + [installed_viewer, fixture_dir / 'rgb16.png', fixture_dir / 'corrupt.bin'],
      depends: worker, env: worker_env, timeout: 30)
    test('GTK view lifecycle and navigation', python, suite: 'integration',
      args: isolated + display + [viewer_test] + viewer_args,
      depends: [worker, fixtures, extra_formats], env: worker_env, timeout: 45)
    if get_option('wayland_tests')
      wayland_env = environment({'MAJORIS_IMAGE_WORKER': worker.full_path(),
        'G_DEBUG': 'fatal-criticals', 'GDK_BACKEND': 'wayland'})
      test('Wayland view lifecycle', python, suite: 'platform',
        args: isolated + [viewer_test] + viewer_args, depends: [worker, fixtures, extra_formats],
        env: wayland_env, timeout: 60)
    endif
    if get_option('extended_tests')
      test('GTK lifecycle stress', python, suite: 'extended',
        args: isolated + ['--repeat', '3'] + display + [viewer_test] + viewer_args,
        depends: [worker, fixtures, extra_formats], env: worker_env, timeout: 160)
      test('tile-coverage-complete', python, suite: 'extended',
        args: isolated + [tile_test, fixtures[0], fixture_dir / 'multipart.exr', '--complete'],
        depends: [worker, fixtures], env: worker_env, timeout: 180)
    endif
  endif
endif
