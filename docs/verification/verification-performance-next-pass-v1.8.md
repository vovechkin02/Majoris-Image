# Majoris Image 2.0.1 — bounded performance acceptance and APNG concurrency pass

**Baseline:** `Majoris-Image(20261119-040730).zip`  
**Engineering standard:** Majoris Cross-Project Engineering Standards v1.8  
**Status:** Source implementation complete; native compilation and runtime qualification **not run** in this editing environment

## Scope and source-level changes

| Component | Change | Expected invariant |
| --- | --- | --- |
| `src/core/image-document.vala` | Serializes APNG cursor publication by request-invocation order under a private mutex | Older overlapping completions cannot replace a newer published session; a later rewind can still publish |
| `tests/integration/test-decoder-selection.vala` | Adds overlapping APNG frame requests and a subsequent rewind/forward check | Public frame output and requested frame timing remain correct |
| `tests/benchmarks/benchmark-viewer-lifecycle.vala` | Loads and keeps eight viewers simultaneously before normal reference release; samples live and settled process RSS over six cycles | No forced `g_object_run_dispose` shortcut; host RSS only, not GPU memory |
| `tests/benchmarks/meson.build` | Explicit Gee dependency for the concurrent-viewer benchmark | Compilation inputs stay discoverable and private |
| `tests/release/collect_performance.py` | Collects independent APNG (10/50/100), scheduler (120/1200/4800, 75% canceled), and viewer RSS measurements | Every workload and output is recorded with source fixture, compiler, codec, and benchmark-harness identity |
| `tests/release/compare_performance.py` | Fail-closed baseline comparison | Missing/unstable/unmatched data and measured regressions cannot produce a pass |
| `tests/release/qualify_performance.py` | Builds/tests Glycin-enabled and disabled profiles, collects measurements, then enforces baseline comparison | `passed` only after a matching baseline and all required stages pass; baseline capture is not a pass |
| `tests/release/prepare_baseline.py` | Copies *only* the matched benchmark harness into an isolated older source tree | Earlier production implementations are not edited or silently benchmarked using different methodology |
| `tests/release/test_compare_performance.py` | Eight native-free policy tests | Regression, missing workload, changed output, incompatible host/harness, noise, and RSS failures are rejected |

## APNG decoder cursor policy

Request order is allocated **before the first asynchronous source check**.
After a decoded frame passes the existing source, color, conversion, and
cancellation checks, its retained decoder is published only if the request
was initiated later than the last successful publication. The root document
keeps a private mutex for the publication and reference snapshot. This avoids
a slower earlier overlapping frame replacing a newer decoder session. It
**does not** promote an invalid or canceled decoder, change public frame output,
or make a global decoder cache. A later user rewind is a newer request and
can publish a cursor positioned at the rewound frame.

No caching of Glycin loader discovery, color/ICC decisions, or source stamps is
included. The already-instrumented stages are now captured individually and
recorded as costs, including source stamp, loader discovery, color probing,
scheduler admission, frame advancement, conversion and final source stamp.
The measurements must demonstrate a specific bottleneck before any future
metadata-caching change can be justified.

## Reproducible paired qualification

Run from this revised source checkout on a Debian 13/Majoris development
machine after installing all documented Glycin, Meson, Vala, GTK, Xvfb and
worker sandbox dependencies. Paths must point to fresh directories outside
the source tree.

```sh
python3 tests/release/prepare_baseline.py \
  --archive '/path/to/Majoris-Image(20261119-040730).zip' \
  --candidate . --destination /tmp/imagekit-baseline-tree \
  --report /tmp/imagekit-baseline-preparation.json

python3 tests/release/qualify_performance.py \
  --source /tmp/imagekit-baseline-tree \
  --workdir /tmp/imagekit-baseline-build \
  --record-baseline /tmp/imagekit-baseline.json \
  --output /tmp/imagekit-baseline-status.json

python3 tests/release/qualify_performance.py \
  --source . --workdir /tmp/imagekit-candidate-build \
  --baseline /tmp/imagekit-baseline.json \
  --output /tmp/imagekit-candidate-status.json
```

The baseline tree retains the earlier production algorithm and applies only
six benchmark-harness files, including the new simultaneous-viewer source.
A SHA-256 for the harness is compared in both result sets, in addition to
CPU model, OS, toolchain, build options, dependency versions, and fixture
fingerprints. Workloads require at least three independent samples (seven by
default). The default maximum median slowdown is **15%**, maximum relative
median absolute deviation is **20%**, and maximum settled viewer RSS growth
is **65,536 KiB**; reviewers may choose stricter explicit release budgets.
These are acceptance-policy defaults, **not measured results**.

`qualify_performance.py` records the full benchmark-suite execution as well
as separately gathered sample JSON and per-command logs. `--record-baseline`
returns `baseline_recorded`, not `passed`; a run without matched baseline is
`blocked`. All other statuses preserve failure and inconclusive evidence.
The policy unit suite can be run without a native build:

```sh
python3 tests/release/test_compare_performance.py
python3 tests/release/check_source_docs.py
```

## Performance and resource qualification limitations

- The APNG stage timings separate repeated metadata/source checks from frame
  advancement; no performance claim follows without a matched completed run.
- The scheduler comparison records queue insertion, arbitrary cancellation,
  and draining separately. The heap implementation and admission policy remain
  unchanged.
- The viewer benchmark holds eight mapped `ImageView` instances concurrently,
  releases their ordinary strong references, then observes host RSS after idle
  quiescence. This does not prove GPU texture reclamation.
- Physical GPU telemetry is a separate test on the intended Majoris
  compositor and graphics hardware. Missing vendor counters, headless Xvfb
  or compositor-only GPU allocations are `not_measured`, never a pass.
- A compiler error, benchmark failure, changed output fingerprint or an
  unstable/noisy comparison prevents performance qualification.

## Checks performed in this editing environment

- Original ZIP extracted and audited against the preceding qualification
  archive; only the bounded files listed above were changed.
- Eight Python comparison acceptance tests passed.
- Python syntax and Vala/native/Python declaration-documentation scans passed.
- A baseline worktree was produced from the previous ZIP and the six allowed
  benchmark files were synchronized; all other files retained their source hash.
- A qualification invocation correctly reported `blocked` for absent Meson
  and `valac`; it did **not** claim that native builds, APNG playback, GPU
  usage or measurements had passed.

**Next release gate:** Run the paired native qualification on Debian 13 with a
real graphical session for any applicable GPU checks. No broader code
architecture or decoder-cache refactor is proposed without a measured defect.
