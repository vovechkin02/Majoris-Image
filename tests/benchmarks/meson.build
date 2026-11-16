benchmark_program = executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
native_benchmark = executable(
  'majoris-image-native-benchmark',
  'benchmark-native.c',
  dependencies: native_api,
)
workloads = [
  'open=' + (fixture_dir / 'rgb16.png'),
  'jpeg=' + (fixture_build_dir / 'navigation.jpg'),
  'large-tiff=' + (fixture_build_dir / 'large.tiff'),
  'pages=' + (fixture_dir / 'multipart.exr'),
  'stream=' + (fixture_dir / 'rgb16.png'),
  'custom=' + (fixture_dir / 'rgb16.png'),
  'cancel=' + (fixture_dir / 'corrupt.bin'),
  'warm=' + (fixture_dir / 'rgb16.png'),
  'thumbnail=' + (
    fixture_dir / 'rgb16.png'
  ),
  'preload=' + (
    fixture_dir / 'rgb16.png'
  ),
  'navigate=' + (
    fixture_dir / 'rgb16.png'
  ),
  'icc=' + (
    fixture_build_dir / 'profiled.png'
  ),
  'tile=' + (
    fixture_build_dir / 'large.tiff'
  ),
  'tiles=' + (
    fixture_build_dir / 'large.tiff'
  ),
  'hdr-sdr=' + (
    fixture_dir / 'multipart.exr'
  ),
  'animation=' + (
    fixture_build_dir / 'animated.gif'
  ),
]
if get_option('glycin').enabled()
  workloads += ['fallback=' + (fixture_build_dir / 'fallback.qoi')]
endif
if get_option('raw_fixtures') != ''
  workloads += [
    'raw-preview=' + (get_option('raw_fixtures') / '350D.CR2'),
    'raw-develop=' + (
      get_option('raw_fixtures') / '350D.CR2'
    ),
  ]
endif
benchmark(
  'Public API performance',
  python,
  suite: 'performance',
  timeout: 900,
  args: isolated + [
    python.full_path(),
    files('run_benchmarks.py'),
    '--build',
    meson.project_build_root(),
    '--worker',
    worker,
    '--fault-worker',
    worker_fault,
    '--executable',
    benchmark_program,
  ] + workloads,
  depends: [fixtures, extra_formats, worker, worker_fault],
)

benchmark(
  'Native pixel output and JPEG XL encoding performance',
  python,
  suite: 'performance',
  timeout: 1800,
  env: {'VIPS_CONCURRENCY': '2'},
  args: isolated + [
    python.full_path(),
    files('run_benchmarks.py'),
    '--build',
    meson.project_build_root(),
    '--executable',
    native_benchmark,
    '--native',
    'mif1-3b-small',
    'mif1-3b-1mp',
    'mif1-3b-4mp',
    'mif1-3b-8mp',
    'mif1-3b-16mp',
    'mif1-4b-small',
    'mif1-4b-1mp',
    'mif1-4b-4mp',
    'mif1-4b-8mp',
    'mif1-4b-16mp',
    'mif1-lazy-16mp',
    'jxl-rgb-4mp',
    'jxl-rgb-16mp',
    'jxl-rgba-4mp',
    'jxl-rgba-16mp',
    'jxl-hdr-lossless',
  ],
)

if get_option('gtk')
  benchmark(
    'GTK tile planning and progressive rendering',
    python,
    suite: 'performance',
    timeout: 900,
    args: isolated + [tile_test, '--benchmark', large_tiff_fixture],
    depends: [worker, fixtures],
    env: worker_env,
  )
  if xvfb.found()
    image_view_benchmark = executable(
      'majoris-image-view-benchmark',
      'benchmark-image-view.vala',
      dependencies: gtk_api,
    )
    view_benchmark_env = {
      'MAJORIS_IMAGE_WORKER': worker.full_path(),
      'G_DEBUG': 'fatal-criticals',
      'GDK_BACKEND': 'x11',
      'GDK_SCALE': '1',
      'GSK_RENDERER': 'cairo',
    }
    benchmark(
      'GTK ImageView snapshots and progressive presentation',
      python,
      suite: 'performance',
      timeout: 300,
      args: isolated + [
        xvfb.full_path(),
        '-a',
        '-s',
        '-screen 0 1920x1200x24',
        image_view_benchmark,
        large_tiff_fixture,
      ],
      depends: [worker, fixtures],
      env: view_benchmark_env,
    )
    if get_option('raw_fixtures') != ''
      benchmark(
        'GTK RAW preview and full development latency',
        python,
        suite: 'performance',
        timeout: 300,
        args: isolated + [
          xvfb.full_path(),
          '-a',
          python.full_path(),
          files('run_benchmarks.py'),
          '--build',
          meson.project_build_root(),
          '--worker',
          worker,
          '--executable',
          image_view_benchmark,
          'raw-first=' + (get_option('raw_fixtures') / '350D.CR2'),
        ],
        depends: [worker],
        env: view_benchmark_env,
      )
    endif
  endif
endif

if glycin.found()
  foreach workload : [
    'small-rgb8',
    'large-rgb8',
    'rgba8',
    'premultiplied-rgba8',
    'gray8',
    'rgb16',
    'rgba16',
    'linear-rgb',
    'large-downscale',
    'fractional-crop',
    'concurrent',
  ]
    benchmark(
      'Glycin pixel conversion (' + workload + ')',
      glycin_pixels_test,
      args: ['--benchmark', workload],
      suite: 'performance',
      timeout: 60,
    )
  endforeach
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
render_options_benchmark = executable(
  'benchmark-render-options',
  ['benchmark-render-options.vala', render_options_source, render_error_source],
  dependencies: [gio, json, math],
  vala_args: render_options_vala_args,
)
foreach workload : ['copy', 'default-comparison']
  benchmark(
    'Render recipe ' + workload,
    render_options_benchmark,
    args: [workload],
    suite: 'performance',
  )
endforeach
