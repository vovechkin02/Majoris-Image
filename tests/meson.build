if get_option('tests')
  exr_tests = executable('test-exr', 'test-exr.cpp', dependencies: native_api)
  test('OpenEXR regions', exr_tests)
  colour_tests = executable('test-imaging', 'test-imaging.c', dependencies: native_api, c_args: native_c_args)
  test('Imaging references', colour_tests, timeout: 150)

  fixture_dir = meson.current_source_dir() / 'fixtures'
  fixture_maker = executable('make-fixtures', 'make-fixtures.c', dependencies: native_api)
  fixtures = custom_target('large and animated fixtures',
    output: ['large.tiff', 'animated.gif'], command: [fixture_maker, '@OUTPUT0@', '@OUTPUT1@'])
  worker_env = environment({'MAJORIS_IMAGE_WORKER': worker.full_path(),
    'G_DEBUG': 'fatal-criticals', 'XDG_CACHE_HOME': meson.current_build_dir() / 'test-cache'})
  executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
  headless = executable('test-headless', 'headless.vala', dependencies: core_api)
  test('public headless API', headless, args: [fixture_dir, fixtures[0]], depends: [worker, fixtures],
    env: worker_env, timeout: 90)

  worker_fault = executable('worker-fault', 'worker-fault.c')
  worker_security = executable('test-worker-security', 'worker-security.vala', dependencies: core_api)
  test('worker timeout, cancellation, crash and response validation', worker_security,
    args: [worker_fault.full_path ()], depends: worker_fault, env: worker_env, timeout: 15)

  if get_option('gtk')
    xvfb = find_program('xvfb-run', required: false)
    viewer_test = executable('test-image-view', 'viewer.vala', dependencies: gtk_api)
    viewer_args = [fixture_dir]
    if glycin.found() and glycin_gtk.found()
      viewer_args += [fixtures[1]]
    endif
    if xvfb.found()
      test('public GTK Builder API', xvfb, args: ['-a', builder_example], env: worker_env)
      test('GTK view lifecycle and navigation', xvfb,
        args: ['-a', viewer_test] + viewer_args, depends: [worker, fixtures], env: worker_env, timeout: 45)
    else
      test('public GTK Builder API', builder_example, env: worker_env)
      test('GTK view lifecycle and navigation', viewer_test,
        args: viewer_args, depends: [worker, fixtures], env: worker_env, timeout: 45)
    endif
  endif
endif
