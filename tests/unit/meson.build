scheduler_source = configure_file(
  input: '../../src/core/job-scheduler.vala',
  output: 'job-scheduler-impl.vala',
  copy: true,
)

exr_tests = executable('test-exr', 'test-exr.cpp', dependencies: native_api)
color_tests = executable(
  'test-imaging',
  'test-imaging.c',
  dependencies: native_api,
  c_args: native_c_args,
)
png_test = executable(
  'test-thumbnail-png',
  'test-thumbnail-png.c',
  dependencies: [core_api, dependency('vips'), test_zlib],
)
scheduler_test = executable(
  'test-preview-scheduler',
  ['test-job-scheduler.vala', scheduler_source],
  dependencies: [gio, gee],
)

test('OpenEXR region decoding', python, args: isolated + [exr_tests], suite: 'unit')
test(
  'Native image processing references',
  python,
  args: isolated + [color_tests],
  suite: 'unit',
  timeout: 150,
)
test('Thumbnail PNG decoding and validation', png_test, suite: 'unit', timeout: 15)
test('Job scheduling and priorities', scheduler_test, suite: 'unit', timeout: 10)

if get_option('gtk')
  policy_source = configure_file(
    input: '../../src/gtk/presentation.vala',
    output: 'presentation-policy-impl.vala',
    copy: true,
  )
  presentation_test = executable(
    'test-presentation-policy',
    ['test-presentation-policy.vala', policy_source],
    include_directories: include_directories('../../src/gtk'),
    dependencies: gtk_api,
  )
  texture_test = executable(
    'test-image-texture',
    'test-image-texture.c',
    include_directories: include_directories('../../src/gtk'),
    dependencies: gtk_api,
  )

  test('HDR presentation policy', presentation_test, suite: 'unit')
  test('GDK floating-point texture color state', texture_test, suite: 'unit')
endif
