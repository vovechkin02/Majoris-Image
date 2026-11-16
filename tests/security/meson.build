fuzz_native = executable('fuzz-native-smoke', 'fuzz-native.c',
  dependencies: [native_api, core_api, test_zlib])
test('native parser fuzz smoke', python, suite: 'security',
  args: isolated + [fuzz_native, fixture_dir / 'rgb16.png', fixture_dir / 'heic-sequence.heic',
    fixture_dir / 'avif-sequence.avif'], timeout: 40)
protocol_smoke = executable('test-protocol-smoke', ['protocol-smoke.vala'] + worker_contract,
  dependencies: [gio, json, math])
test('worker protocol properties', protocol_smoke, suite: 'security',
  env: {'G_DEBUG': 'fatal-criticals'}, timeout: 15)
if get_option('libfuzzer')
  executable('fuzz-imagekit', 'fuzz-native.c',
    dependencies: [native_api, core_api, test_zlib],
    c_args: ['-DIMAGEKIT_LIBFUZZER', '-fsanitize=fuzzer'],
    link_args: ['-fsanitize=fuzzer'])
endif
worker_fault = executable('worker-fault', 'worker-fault.c')

worker_security = executable('test-worker-security', 'worker-security.vala', dependencies: core_api)
test('worker timeout, cancellation, crash and response validation', python, suite: 'security',
  args: isolated + [worker_security, worker_fault.full_path()], depends: worker_fault,
  env: worker_env, timeout: 15)

publish_gate = shared_module('cache-publish-gate', 'cache-publish-gate.c',
  dependencies: meson.get_compiler('c').find_library('dl'))
test('cross-process thumbnail publication', python, suite: 'security',
  args: isolated + [python, files('cache_processes.py'), thumbnailer, fixture_dir / 'rgb16.png', publish_gate],
  depends: [worker, thumbnailer, publish_gate], env: worker_env, timeout: 40)
