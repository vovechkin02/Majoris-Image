benchmark_program = executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
native_benchmark = executable(
  'majoris-image-native-benchmark',
  'benchmark-native.c',
  dependencies: native_api,
)
workloads = [
  'open=' + (fixture_dir / 'rgb16.png'),
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
    '--executable',
    benchmark_program,
  ] + workloads,
  depends: [fixtures, worker],
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
