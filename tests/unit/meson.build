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

if glycin.found()
  glycin_pixels_test = executable(
    'test-glycin-pixels',
    ['test-glycin-pixels.c', '../../src/core/glycin-pixels.c'],
    dependencies: [glycin, gio, math],
    link_args: [
      '-Wl,--wrap=gly_frame_get_width',
      '-Wl,--wrap=gly_frame_get_height',
      '-Wl,--wrap=gly_frame_get_stride',
      '-Wl,--wrap=gly_frame_get_memory_format',
      '-Wl,--wrap=gly_frame_get_buf_bytes',
      '-Wl,--wrap=g_cancellable_set_error_if_cancelled',
      '-Wl,--wrap=g_try_malloc_n',
    ],
  )
  test('Glycin frame layouts and resampling', glycin_pixels_test, suite: 'unit')
endif

render_options_source = configure_file(
  input: '../../src/core/render-options.vala',
  output: 'render-options-impl.vala',
  copy: true,
)
render_error_source = configure_file(
  input: '../../src/core/image-error.vala',
  output: 'image-error-impl.vala',
  copy: true,
)
render_options_test = executable(
  'test-render-options',
  ['test-render-options.vala', render_options_source, render_error_source],
  dependencies: [gio, json, math],
  vala_args: render_options_vala_args,
)
test('Render recipe copying and compatibility', render_options_test, suite: 'unit')

sniff_stream_source = configure_file(
  input: '../../src/core/source-sniff-stream.vala',
  output: 'source-sniff-stream-impl.vala',
  copy: true,
)
sniff_stream_test = executable(
  'test-source-sniff-stream',
  ['test-source-sniff-stream.vala', sniff_stream_source],
  dependencies: gio,
)
test('Source sniffing stream closure and error precedence', sniff_stream_test, suite: 'unit')
export_transfer_test = executable(
  'test-export-transfer',
  ['test-export-transfer.c', '../../src/core/export-transfer.c'],
  dependencies: core_api,
  link_args: [
    '-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=g_close,--wrap=renameat,--wrap=renameat2',
    '-Wl,--wrap=openat,--wrap=openat64,--wrap=fstat,--wrap=fstat64,--wrap=fchown,--wrap=fchmod',
    '-Wl,--wrap=stat,--wrap=stat64',
  ],
)
test('Encoded export transfer validation and publication', export_transfer_test, suite: 'unit')
