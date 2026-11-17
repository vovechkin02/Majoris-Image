# Glycin 16-bit transfer conversion

## A. Implementation

The unchanged converter at `ed75f3f858a7bf62e0f61322f2e5bb6e711716ec`
selects channel offsets and depth from the actual Glycin frame format. It
validates dimensions, stride, and buffer extent, then rounds the crop and scaled
output dimensions and allocates an RGBA32F buffer. In the GTask worker it
samples the cropped area or the nearest source pixel. It normalizes native
16-bit integers by 65535, skips zero alpha, and reconstructs and clamps
premultiplied colors before transfer decoding. Encoded RGB uses the piecewise
sRGB function, grayscale ICC uses the 2.2 exponent, and linear samples pass
through as normalized values. It accumulates coverage times alpha times linear
color in double precision, divides color by accumulated alpha, and divides
alpha by total coverage. The worker publishes the float32 buffer through the
existing finish function.

The retained candidate uses separate static double tables for sRGB and
grayscale ICC. Each table is populated by the original transfer function and
published independently using GLib's one-time initialization guard. A valid
encoded, straight 16-bit frame uses a table when the crop covers at least
32,768 source pixels for area sampling, or at least that many destination
pixels for nearest sampling. The cutoff estimates work before decoding and
avoids a cold fill for small previews. Cropped area is a conservative proxy
because fractional cells can revisit source pixels. Transparent pixels can
reduce actual transfer work. The existing 8-bit tables keep their original
eligibility. Premultiplied input and small encoded 16-bit inputs use the
formula, and linear input uses the normalized sample.

The converter checks cancellation immediately before a possible 16-bit table
fill and at the existing source-row checkpoints. A worker canceled during a
fill completes publication before observing cancellation, allowing other
workers to use the complete immutable table. Two tables reserve 1 MiB in
zero-fill storage plus their guards. Each first use touches 512 KiB, with no
per-frame table allocation or allocation failure path. Each conversion still
allocates one GTask and a buffer of `16 × output width × output height` bytes.

## B. Real Glycin frames

Debian 13 under WSL2, Linux 6.18.58.1, AMD Ryzen 7 7700X, GCC 14.2.0, Meson
1.7.0, GLib 2.84.4, libglycin 1.2.1, glycin-loaders 1.2.1+ds-2, and Bubblewrap
0.12.0. The probe used `gly_frame_get_memory_format()` after loading with the
production Bubblewrap selector. The four 16-bit PNG fixtures carry explicit
sRGB declarations and retain source depth. Opaque formats have implicit
alpha and both alpha PNGs return straight alpha. PNGs, TIFF, JPEG XL, and an
8 × 8 HDR AVIF frame all returned integer formats in this environment.

| Source | Actual Glycin format | Conversion and result |
| --- | --- | --- |
| RGB16 PNG | `GLY_MEMORY_R16G16B16` | sRGB16 table, exact |
| RGBA16 PNG | `GLY_MEMORY_R16G16B16A16` | sRGB16 table and straight alpha, exact |
| Grayscale16 PNG | `GLY_MEMORY_G16` | sRGB16 table, exact |
| Grayscale-alpha16 PNG | `GLY_MEMORY_G16A16` | sRGB16 table and straight alpha, exact |
| Grayscale ICC PNG, 2 × 1 | `GLY_MEMORY_G16A16` | Grayscale ICC formula below the cutoff, exact |
| Grayscale ICC PNG, 1601 × 1203 | `GLY_MEMORY_G16A16` | Loader-level probe with grayscale ICC table, exact |
| Profiled RGB16 TIFF, 2 × 1 | `GLY_MEMORY_R16G16B16` | Loader-level probe with sRGB formula, exact |
| Lossless RGB16 JPEG XL | `GLY_MEMORY_R16G16B16` | Loader-level probe with sRGB16 table, exact |
| HDR AVIF, 8 × 8 | `GLY_MEMORY_R16G16B16` | Memory format observed, no SDR conversion benchmark |

The TIFF and JPEG XL measurements exercise frame loading and conversion on
their own. The public comparison uses the full PNG fallback with
`COLOR_MANAGED` required and the real worker verifying the sRGB declaration.
The profiled grayscale PNG also exercises the existing ICC behavior through
the public integration test. No sampled real frame was premultiplied, so the
two 16-bit premultiplied layouts have synthetic coverage.

## C. Numerical verification

The six converter tests cover all 18 layouts, all 65,536 16-bit samples for
each transfer, exact float32 output, misaligned rows, linear bypass, alpha
from zero through 1/65535 to opaque, and premultiplied reconstruction.
Concurrent jobs cover shared and independent table initialization, mixed
8-bit and 16-bit work, and cancellation during accumulation while other work
completes. The retained fault cases cover unsupported layout, invalid
extents, stride, short input, preview limit, and allocation failure. An
isolated initializer probe also compared all 131,072 double entries byte for
byte against the formula.

The same test harness was linked to the unchanged converter. Complete
RGBA32F SHA-256 values matched its outputs for every measured workload,
including fractional crop and downscale, premultiplied layouts, and both
outputs of the mixed concurrent workload. The observed maximum difference
was zero. Public tests also verify representative RGB, grayscale, alpha and
crop values with the existing fallback color and capability checks active.

| Reference buffer | SHA-256 |
| --- | --- |
| Synthetic RGB16 | `d9324c70f5c0a34c9972ee134497a40f33fd827d9e2fc378ff090e5fa947c84d` |
| Synthetic RGBA16 | `c657ff00eae254401e09d378a26506f767f649677e018ae1bd4117c5ec525760` |
| Synthetic grayscale ICC | `fe98b22d34f6ef5c77e1d4e70d80f18be4c4d200d50e92cbfc10b9c638fbe3ac` |
| Synthetic grayscale-alpha ICC | `26d282ab74d09399a9e1d9a450d82adac09dcdde010c76d10ecbe79e5ea61d21` |
| Synthetic premultiplied RGBA16 | `9a4c87e7339d12bb8f85aca5784aef0a4bafe5d6ae48bf66d3b23b212bf54f5e` |
| Synthetic premultiplied grayscale-alpha16 | `a7bfce20ef17ded88832270b4466c7fbd9708270d540517ac0c7277ae6699349` |
| Synthetic fractional RGB16 | `bced725f2efc86891354a1d9712d0d81764cf44a391e623fb230cfb2514c683a` |
| Public sRGB RGB16 PNG | `bf887f584bcd4421b877a5adc54dfb0383728ea1f800c0a8cca22de95f097c3b` |
| Public sRGB RGBA16 PNG | `10bb3affa1f25119b553a23ead00577770db070e40d89ed8b4a79ab6edc9b5de` |
| Public sRGB grayscale16 PNG | `2e2f289e5427dd79f9114fbfff1a98b30efbcb11532e347e696b8e94264fff17` |
| Public sRGB grayscale-alpha16 PNG | `99e1b1c9602c795d4a29a5ed5a42153a4225bc946a301da4f72c688d6b01fb79` |
| Large grayscale ICC PNG loader probe | `893e09ddce0ef8185dcca8b6932c4fc2d39d61e894556c848a2f891d86508cbe` |

## D. Performance

Each synthetic case used three fresh processes per variant, with five warm
conversions per process or 100 for the 31 × 19 cases. Times and throughput are
medians. Peak RSS is the maximum process high-water sample. Values are
reference → candidate. Both converter builds used GCC `-O2`.

| Workload | Cold wall (ms) | Warm wall (ms) | Warm CPU (ms) | Warm MP/s | Peak RSS (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small RGB8 | 0.416 → 0.410 | 0.072 → 0.068 | 0.062 → 0.059 | 8.2 → 8.7 | 6,180 → 6,188 |
| Large RGB8 | 27.72 → 28.76 | 17.04 → 17.59 | 17.05 → 17.56 | 113.0 → 109.5 | 41,900 → 41,896 |
| RGBA8 | 17.10 → 16.47 | 14.67 → 14.70 | 14.65 → 14.68 | 131.3 → 131.0 | 19,464 → 19,504 |
| Grayscale ICC 8 | 15.79 → 17.27 | 14.01 → 14.09 | 14.01 → 14.08 | 137.5 → 136.7 | 13,772 → 13,888 |
| Premultiplied RGBA8 | 141.20 → 140.54 | 146.25 → 140.84 | 146.24 → 140.84 | 13.2 → 13.7 | 19,428 → 19,456 |
| Small RGB16 | 0.443 → 0.455 | 0.076 → 0.076 | 0.066 → 0.066 | 7.8 → 7.8 | 6,140 → 6,120 |
| RGB16 at cutoff, 16 × 8 output | 1.853 → 1.591 | 1.201 → 0.149 | 1.191 → 0.132 | 27.3 → 219.9 | 6,376 → 6,900 |
| Large RGB16 | 157.82 → 18.53 | 153.95 → 14.99 | 150.06 → 14.97 | 12.5 → 128.5 | 23,144 → 23,812 |
| Large RGBA16 | 126.16 → 18.51 | 125.71 → 15.28 | 125.69 → 15.27 | 15.3 → 126.0 | 27,040 → 27,496 |
| Grayscale ICC 16 | 140.08 → 17.67 | 133.83 → 15.10 | 133.82 → 15.09 | 14.4 → 127.6 | 15,808 → 16,304 |
| Grayscale-alpha ICC 16 | 114.85 → 18.83 | 114.60 → 15.37 | 114.59 → 15.36 | 16.8 → 125.3 | 19,456 → 19,972 |
| Premultiplied RGBA16 | 133.94 → 129.22 | 129.02 → 131.09 | 129.01 → 130.14 | 14.9 → 14.7 | 26,920 → 27,228 |
| Premultiplied grayscale-alpha16 | 119.60 → 116.47 | 116.73 → 115.15 | 116.32 → 115.13 | 16.5 → 16.7 | 19,516 → 19,600 |
| Linear RGB16 | 21.12 → 22.49 | 19.26 → 20.45 | 19.25 → 20.44 | 100.0 → 94.2 | 23,024 → 22,972 |
| Fractional RGB16 downscale | 48.97 → 5.12 | 48.07 → 3.61 | 48.06 → 3.60 | 23.0 → 306.3 | 17,672 → 18,164 |
| Two RGB16 conversions | 162.86 → 18.77 | 154.47 → 14.89 | 305.95 → 29.37 | 24.9 → 258.7 | 29,032 → 29,536 |
| Concurrent RGB16 and grayscale ICC | 153.62 → 18.42 | 150.31 → 15.10 | 285.41 → 29.87 | 25.6 → 255.0 | 32,716 → 33,820 |
| Linear RGB8 | 26.09 → 23.99 | 24.40 → 22.18 | 24.39 → 22.15 | 78.9 → 86.8 | 17,324 → 17,456 |
| Large RGB8 downscale | 13.79 → 13.30 | 12.04 → 12.29 | 12.04 → 12.28 | 353.6 → 346.6 | 18,920 → 18,876 |
| Fractional RGBA8 crop | 14.89 → 14.94 | 12.53 → 12.47 | 12.52 → 12.45 | 88.3 → 88.7 | 20,516 → 20,544 |
| Two RGB8 conversions | 16.36 → 16.72 | 14.07 → 14.10 | 27.93 → 27.94 | 273.7 → 273.3 | 23,388 → 23,328 |

Fresh harness startup through `--help` measured 2.61 → 2.65 ms, median of 21.
Direct initialization measured 0.86 ms for sRGB and 0.73 ms for grayscale
ICC, each as the first table in 21 fresh processes. Initial RSS was 5,248 KiB
in both cases. The first fill raised RSS by about 940 KiB including first use
of shared code and runtime pages. The second fill raised RSS by 508–512 KiB.
The executable's table storage is zero-fill, and paired small and linear
cases showed no corresponding residency increase. In the mixed concurrent
case peak RSS increased by 1,104 KiB. The linear RGB16 path measured 6% slower
in the final run despite bypassing both tables. The large RGB8 case measured
3% slower, with other regular 8-bit table cases within 2% and a separate
paired run measuring large RGB8 within 1%.

Real-frame runs used one cold and five warm conversions for each variant.
Every iteration loads a fresh frame. The large sources are 1601 × 1203 and
produce 701 × 527 previews. CPU and RSS are for the converting process.
Loader time includes opening the image and obtaining its first frame.

| Real source | Cold loader / conversion (ms) | Warm loader (ms) | Warm conversion (ms) | Warm total / CPU (ms) | Peak RSS (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| RGB16 PNG | 19.22 / 95.03 → 19.81 / 18.60 | 19.55 → 19.01 | 94.27 → 15.89 | 113.15 / 96.71 → 35.36 / 18.13 | 27,640 → 28,092 |
| RGBA16 PNG | 24.24 / 87.72 → 24.00 / 17.12 | 23.42 → 23.56 | 87.05 → 14.76 | 110.29 / 89.52 → 37.84 / 17.18 | 31,216 → 31,588 |
| Grayscale16 PNG | 11.91 / 97.43 → 11.21 / 18.23 | 11.27 → 11.10 | 94.02 → 15.82 | 105.28 / 96.64 → 26.99 / 17.90 | 19,900 → 20,600 |
| Grayscale-alpha16 PNG | 15.31 / 88.01 → 16.27 / 16.43 | 15.35 → 15.12 | 87.28 → 14.83 | 102.63 / 89.37 → 29.95 / 16.96 | 23,844 → 24,184 |
| Profiled grayscale PNG | 7.67 / 0.24 → 8.42 / 0.22 | 6.44 → 6.45 | 0.075 → 0.084 | 6.51 / 1.99 → 6.57 / 2.06 | 10,644 → 10,768 |
| Large grayscale-alpha ICC PNG | 40.98 / 82.52 → 39.96 / 16.69 | 40.08 → 39.05 | 76.51 → 14.33 | 116.59 / 102.38 → 54.20 / 40.02 | 24,124 → 24,500 |
| Profiled TIFF | 7.40 / 0.31 → 7.52 / 0.23 | 5.96 → 5.71 | 0.081 → 0.079 | 6.04 / 1.79 → 5.83 / 1.66 | 10,316 → 10,528 |
| RGB16 JPEG XL | 141.41 / 95.71 → 139.04 / 19.22 | 139.68 → 142.14 | 97.48 → 15.94 | 237.03 / 132.73 → 158.09 / 52.27 | 28,716 → 29,200 |

The public fallback used seven fresh processes per variant for each PNG
layout. Process wall time includes startup and hashing. CPU includes waited
children. All 56 samples produced a 1600 × 1202 preview with the same
per-fixture hash and verified sRGB diagnostic as their reference.

| Public sRGB source | Open (ms) | Process wall (ms) | Process CPU (ms) | Peak RSS (KiB) |
| --- | ---: | ---: | ---: | ---: |
| RGB16 PNG | **280.70 → 123.20** | 367.80 → 204.90 | 282.14 → 126.86 | 52,744 → 53,288 |
| RGBA16 PNG | 301.01 → 148.09 | 391.41 → 246.75 | 298.42 → 143.55 | 56,636 → 57,180 |
| Grayscale16 PNG | 299.79 → 128.10 | 391.23 → 206.59 | 307.78 → 132.14 | 45,392 → 45,672 |
| Grayscale-alpha16 PNG | 330.94 → 127.32 | 433.31 → 205.50 | 330.69 → 132.87 | 48,840 → 49,348 |

## E. Reproduction and regression checks

Use the same harness and flags with both converters from the repository root.
The baseline below is the original converter, including its 8-bit tables.

```sh
study=$(mktemp -d)
git show ed75f3f858a7bf62e0f61322f2e5bb6e711716ec:src/core/glycin-pixels.c > "$study/reference.c"
for variant in reference candidate; do
  source="$study/reference.c"
  if [ "$variant" = candidate ]; then source=src/core/glycin-pixels.c; fi
  gcc -O2 -Wall -Wextra -Wpedantic -Werror -o "$study/$variant" \
    tests/unit/test-glycin-pixels.c "$source" \
    $(pkg-config --cflags --libs glycin-1 gio-2.0) -lm \
    -Wl,--wrap=gly_frame_get_width,--wrap=gly_frame_get_height \
    -Wl,--wrap=gly_frame_get_stride,--wrap=gly_frame_get_memory_format \
    -Wl,--wrap=gly_frame_get_buf_bytes,--wrap=g_cancellable_set_error_if_cancelled \
    -Wl,--wrap=g_try_malloc_n
  "$study/$variant"
  "$study/$variant" --benchmark rgb16
done
```

Generate large fixtures separately from the fast suite, then run the
sandboxed frame probe. The optional JPEG XL sample used the installed
`vips` tool with `vips copy rgb16.png 'rgb16.jxl[lossless=true]'`.
The large grayscale ICC sample replaced the generated grayscale-alpha PNG's
`sRGB` chunk with the `iCCP` chunk from the existing `gray-profile.png`.

```sh
python3 -B - "$study" <<'PY'
import sys
from pathlib import Path

from tests.support.make_formats import write_srgb16_png

root = Path(sys.argv[1])
for name, kind in (("rgb", 2), ("rgba", 6), ("gray", 0), ("gray-alpha", 4)):
    write_srgb16_png(root / f"{name}16.png", kind, 1601, 1203)
PY
"$study/reference" --real-frame "$study/rgb16.png" srgb
"$study/candidate" --real-frame "$study/rgb16.png" srgb
python3 -B tests/benchmarks/run_benchmarks.py --build build \
  --worker build/src/worker/majoris-image-worker \
  --fault-worker build/tests/security/worker-fault \
  --executable build/tests/benchmarks/majoris-image-benchmark \
  --samples 7 --output "$study/candidate-public.json" "fallback16=$study/rgb16.png"
```

For the public baseline, use that last command with
`LD_LIBRARY_PATH=/absolute/baseline/src/core` and a different JSON path.
The baseline Core came from the unchanged release build. The Vala harness,
fault backend, and normal worker were identical for both runs.

| Verification command or profile | Result |
| --- | --- |
| `meson test -C base --print-errorlogs` and same default GTK configuration with candidate | 25 passed and one unconfigured RAW skip each. |
| `meson test -C full --print-errorlogs` with extended tests, vendor Ultra HDR and manifest RAW inputs | 28 passed, zero failures or skips. |
| `python3 tests/release/qualify.py headless --variant headless` | 12 required tests passed. Glycin-enabled Core exported 285 symbols and had no GTK dependency. |
| `python3 tests/release/qualify.py native --variant native` | 22 required tests passed. Glycin-disabled Core exported 280 applicable symbols. |
| Focused GCC build with `-fsanitize=address,undefined -O1 -g -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1` | All six converter tests passed. |
| `meson setup strict -Dwarning_level=3 -Dwerror=true -Dglycin=enabled` and `meson compile -C strict` | Full build stopped at existing Vala warnings promoted to errors. The touched C sources passed the standalone strict GCC command above. |
| `python3 tests/release/check_abi.py full/src/core/libmajoris-image.so.2 full/src/gtk/libmajoris-image-gtk4.so.2` | Both ABI 2 checks passed with 285 Core and 77 GTK exports. Baseline and candidate VAPIs, GIRs, and generated headers matched byte for byte. |
| `dpkg-buildpackage -us -uc -b` and `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | Each build produced five binary packages and four debug-symbol packages. |
| `python3 tests/release/qualify.py full --installed` | 25 required tests passed, with 28 tests and 35 performance registrations passing in total. Installed Headless Core, Vala GTK, C GtkBuilder, GI, Photos export, and thumbnail consumers passed. |

The existing test names, suites, and conditions remain in place. New
coverage runs under the existing Glycin unit and decoder-selection test
registrations. Additional performance registrations run in benchmark mode.
Installed qualification used the vendor packages extracted in a private mount
namespace. Running the thumbnailer post-install script in that namespace
generated the descriptor checked by the installed tests. GTK testing used
X11 at scales 1 and 2. Wayland and physical HDR display checks were not run.

## F. Decision

The real-frame and public comparisons justify the selective tables. Static
storage avoids a per-process heap allocation and allows unused table pages to
remain untouched. The cutoff trades away warm lookup on small images for
bounded first-use overhead and avoids filling the tables in processes that
handle only small frames. Lower-precision tables, interpolation, SIMD, and
other sample depths were deferred. Straight lookup for premultiplied input
would change results because division by alpha introduces values between the
integer index levels.
