benchmark_program = executable('majoris-image-benchmark', 'benchmark.vala', dependencies: core_api)
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
