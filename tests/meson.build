if get_option('tests')
  exr_tests = executable('test-exr', 'test-exr.cpp', dependencies: native_api)
  test('OpenEXR regions', exr_tests)
  colour_tests = executable('test-imaging', 'test-imaging.c', dependencies: native_api, c_args: native_c_args)
  test('Imaging references', colour_tests, timeout: 150)

  fixture_dir = meson.current_source_dir() / 'fixtures'
  fixture_maker = executable('make-fixtures', 'make-fixtures.c', dependencies: native_api)
  fixtures = custom_target('large and animated fixtures',
    output: ['large.tiff', 'animated.gif', 'animated.webp', 'jpeg2000.jp2', 'profiled.png', 'profiled.tiff'],
    command: [fixture_maker, '@OUTPUT@'])
  python = find_program('python3')
  extra_formats = custom_target('legacy and APNG fixtures',
    input: 'make-formats.py', output: ['fallback.qoi', 'partial.apng',
      'orientation-1.tiff', 'orientation-2.tiff', 'orientation-3.tiff', 'orientation-4.tiff',
      'orientation-5.tiff', 'orientation-6.tiff', 'orientation-7.tiff', 'orientation-8.tiff',
      'fallback.bmp', 'fallback.ico', 'fallback.tga', 'fallback.dds', 'fallback.pgm', 'invalid-profile.png'],
    command: [python, '@INPUT@', '@OUTPUT@'])
  decoder_test = executable('test-decoders', 'decoders.vala', dependencies: core_api)

  worker_env = environment({'MAJORIS_IMAGE_WORKER': worker.full_path(),
    'G_DEBUG': 'fatal-criticals', 'XDG_CACHE_HOME': meson.current_build_dir() / 'test-cache'})
  executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
  headless = executable('test-headless', 'headless.vala', dependencies: core_api)
  test('public headless API', headless, args: [fixture_dir, fixtures[0]], depends: [worker, fixtures],
    env: worker_env, timeout: 90)

  worker_fault = executable('worker-fault', 'worker-fault.c')
  test('decoder selection and composited APNG frames', decoder_test,
    args: [extra_formats[0], extra_formats[1], get_option('glycin').enabled() ? 'enabled' : 'optional', worker_fault.full_path(),
      fixture_dir / 'avif-sequence.avif', avif_sequence.found() ? 'avif' : 'optional',
      heif_sequence ? fixture_dir / 'heic-sequence.heic' : '', fixture_dir / 'avif-hdr-sequence.avif', thumbnail_registration],
    depends: [worker, worker_fault, extra_formats, fixtures], env: worker_env)
  test('desktop thumbnail registration and sample output', python,
    args: [files('thumbnail-registration.py'), thumbnailer, thumbnail_registration, fixture_dir, meson.current_build_dir(),
      thumbnail_mime_generator, meson.project_source_root() / 'src/thumbnail/majoris-image.thumbnailer.in',
      glycin.found() ? 'glycin' : 'native'],
    depends: [worker, thumbnailer, thumbnail_registration, fixtures, extra_formats], env: worker_env, timeout: 30)
  test('cross-process thumbnail publication', python,
    args: [files('cache-processes.py'), thumbnailer, fixture_dir / 'rgb16.png'],
    depends: [worker, thumbnailer], env: worker_env, timeout: 30)
  if get_option('raw_fixtures') != ''
    raw_samples = executable('test-raw-samples', 'raw-samples.vala', dependencies: core_api)
    test('real camera RAW', raw_samples, args: [get_option('raw_fixtures')],
      depends: worker, env: worker_env, timeout: 180)
  endif

  worker_security = executable('test-worker-security', 'worker-security.vala', dependencies: core_api)
  test('worker timeout, cancellation, crash and response validation', worker_security,
    args: [worker_fault.full_path ()], depends: worker_fault, env: worker_env, timeout: 15)

  if get_option('gtk')
    xvfb = find_program('xvfb-run', required: false)
    policy_source = configure_file(input: '../src/gtk/presentation.vala',
      output: 'presentation-policy-impl.vala', copy: true)
    presentation_test = executable('test-presentation-policy',
      ['presentation.vala', policy_source],
      include_directories: include_directories('../src/gtk'), dependencies: gtk_api)
    test('HDR presentation policy', presentation_test)
    viewer_test = executable('test-image-view', 'viewer.vala', dependencies: gtk_api)
    viewer_args = [fixture_dir, fixtures[1], fixtures[2]]
    if avif_sequence.found()
      viewer_args += [fixture_dir / 'avif-sequence.avif']
    endif
    if heif_sequence
      viewer_args += [fixture_dir / 'heic-sequence.heic']
    endif
    if glycin.found() and glycin_gtk.found()
      viewer_args += [extra_formats[1]]
    endif
    files_args = [fixture_dir / 'rgb16.png', fixture_dir / 'multipart.exr']
    photos_args = [fixtures[0], fixture_dir / 'multipart.exr', meson.current_build_dir() / 'photos-export.png']
    test('Files public preview workflow', xvfb.found() ? xvfb : files_preview,
      args: (xvfb.found() ? ['-a', files_preview] : []) + files_args,
      depends: [worker, files_preview], env: worker_env, timeout: 90)
    test('Photos public full-quality workflow', xvfb.found() ? xvfb : photos_viewer,
      args: (xvfb.found() ? ['-a', photos_viewer] : []) + photos_args,
      depends: [worker, fixtures, photos_viewer], env: worker_env, timeout: 90)
    if xvfb.found()
      test('public GTK Builder API', xvfb,
        args: ['-a', builder_example, fixture_dir / 'rgb16.png'], depends: worker, env: worker_env)
      test('GTK view lifecycle and navigation', xvfb,
        args: ['-a', viewer_test] + viewer_args, depends: [worker, fixtures, extra_formats], env: worker_env, timeout: 45)
    else
      test('public GTK Builder API', builder_example,
        args: [fixture_dir / 'rgb16.png'], depends: worker, env: worker_env)
      test('GTK view lifecycle and navigation', viewer_test,
        args: viewer_args, depends: [worker, fixtures, extra_formats], env: worker_env, timeout: 45)
    endif
  endif
endif
