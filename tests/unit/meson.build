scheduler_source = configure_file(input: '../../src/core/job-scheduler.vala',
  output: 'job-scheduler-impl.vala', copy: true)

exr_tests = executable('test-exr', 'test-exr.cpp', dependencies: native_api)
color_tests = executable('test-imaging', 'test-imaging.c',
  dependencies: native_api, c_args: native_c_args)
png_test = executable('test-thumbnail-png', 'thumbnail-png.c',
  dependencies: [core_api, dependency('vips'), test_zlib])
scheduler_test = executable('test-preview-scheduler', ['scheduler.vala', scheduler_source],
  dependencies: [gio, gee])

test('OpenEXR regions', python, args: isolated + [exr_tests], suite: 'unit')
test('Imaging references', python, args: isolated + [color_tests], suite: 'unit', timeout: 150)
test('thumbnail PNG decode and validation', png_test, suite: 'unit', timeout: 15)
test('foreground, tile and preload scheduling', scheduler_test, suite: 'unit', timeout: 10)

if get_option('gtk')
  policy_source = configure_file(input: '../../src/gtk/presentation.vala',
    output: 'presentation-policy-impl.vala', copy: true)
  presentation_test = executable('test-presentation-policy',
    ['presentation.vala', policy_source],
    include_directories: include_directories('../../src/gtk'), dependencies: gtk_api)
  texture_test = executable('test-image-texture', 'image-texture.c',
    include_directories: include_directories('../../src/gtk'), dependencies: gtk_api)

  test('HDR presentation policy', presentation_test, suite: 'unit')
  test('GDK floating-point texture color state', texture_test, suite: 'unit')
endif
