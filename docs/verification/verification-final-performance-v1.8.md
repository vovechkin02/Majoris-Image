# Majoris Image 2.0.1 final performance qualification

**Release qualification is inconclusive.** The native test and benchmark
suites, matched APNG timings, preview memory gates, and real-camera RAW
qualification passed. Tiled viewer counters blocked the full comparison.
The initial scheduler timing failure did not recur in an interleaved
repeat. Physical GPU residency was not measured.

## Scope and provenance

The candidate was the working tree at `1c248cc` with its local memory and
platform qualification changes. Tests ran on Debian 13.5 under WSL2 kernel
`6.18.58.1-microsoft-standard-WSL2` using Meson 1.7.0 and Vala 0.56.18.
Both native profiles enabled GTK4, sequenced decoding, and introspection.
Ultra HDR support was detected in both profiles.

The reference was the earlier local archive
`Majoris-Image-2.0.1-matched-platform-qualification-v1.8.zip`, whose
SHA-256 was `3b125f45eff87b8b608824fea9713ffdca6c572e08bfd669ced5cd57cfe625fe`.
The preparation tool copied the candidate benchmark harness and verified the
remaining archive files. The reference and candidate production-source
digests were both
`34976ba9d95f927688900bccc4fd8a29d55be185934cdac15628954f390e76c4`.
This comparison checks repeatability for identical production source with
matching instrumentation.

## Native builds and suites

| Candidate profile | Compile | Unit, integration, and security | Registered benchmarks | Real-camera RAW |
|---|---|---:|---:|---:|
| Glycin enabled | Passed | 34 passed | 40 passed | 9 passed |
| Glycin disabled | Passed | 33 passed | 13 passed | 9 passed |

Both profiles completed all registered entries with zero failures and zero
skips. The Glycin-only conversion and sandbox loading registrations apply
to the enabled profile. The enabled and disabled Public API entries took
108.40 and 58.62 seconds, respectively. The eight-viewer tiled entries
took 142.54 and 142.91 seconds.

The initial archive-based command completed reference compilation, then failed
seven integration entries in each profile. The affected entries were viewport
tile coverage, decoder selection, desktop thumbnail registration, GTK view
lifecycle, the headless Core API, and the Files and Photos public workflows.
The logs identified missing generated inputs including `large.tiff`,
`orientation-1.tiff`, `still.webp`, and `navigation.jpg`. The only code
change was to let Meson build the test dependencies in
`tests/release/qualify_performance.py`. A clean `meson compile` builds the
executables, and the subsequent default `meson test` builds the fixtures.
Re-running the reference tests with dependency rebuilding yielded 34
passes with Glycin enabled and 33 with Glycin disabled. This exercised
the generated fixtures that the first invocation skipped.

All nine exact CC0 samples were acquired by manifest SHA-256 and passed
the repository preflight. Each profile then passed the existing
interoperability test, including source precision, decoded geometry,
embedded preview, and checksums. Each run exercised nine embedded
previews. [RAW fixture catalog](https://raw.pixls.us/)

The RAW-enabled builds also completed the registered GTK RAW benchmark
and the Public API RAW preview and development workloads. Public API values
below are five-sample medians. GTK values are Meson entry wall times.

| Profile | RAW preview (s) | RAW develop (s) | GTK RAW entry (s) |
|---|---:|---:|---:|
| Glycin enabled | 0.228633 | 0.631116 | 5.50 |
| Glycin disabled | 0.236846 | 0.649488 | 5.42 |

Reference and candidate exported symbol sets matched for both Core and GTK
in each profile. Core exported 285 symbols with Glycin enabled and 280
with it disabled. GTK exported 77 in both.

## Matched APNG and scheduler timings

Each revision supplied seven independent samples per workload. Both
enabled builds used worker profiling and the same reported toolchain,
codec versions, fixture identities, and harness SHA-256
`493c126021e12e53dbfce82180f9896a0cad0be27b8fc9863df657063c73ca84`. All four APNG
decoded-output hashes matched. The table shows medians and the
repository's timing-only evaluation with its 15% regression limit.

| Workload | Reference (ms) | Candidate (ms) | Change | Timing gate |
|---|---:|---:|---:|---|
| animation-apng-10 | 518.069 | 518.622 | +0.11% | passed |
| animation-apng-50 | 2487.124 | 2489.405 | +0.09% | passed |
| animation-apng-100 | 4961.597 | 4935.682 | -0.52% | passed |
| animation-apng-composite | 1512.456 | 1489.650 | -1.51% | passed |
| scheduler-120-queue | 0.139 | 0.135 | -2.88% | passed |
| scheduler-120-cancel | 0.166 | 0.169 | +1.81% | passed |
| scheduler-120-drain | 0.165 | 0.156 | -5.45% | passed |
| scheduler-1200-queue | 1.369 | 1.614 | +17.90% | failed |
| scheduler-1200-cancel | 4.309 | 4.296 | -0.30% | passed |
| scheduler-1200-drain | 1.678 | 1.684 | +0.36% | passed |
| scheduler-4800-queue | 5.700 | 5.418 | -4.95% | passed |
| scheduler-4800-cancel | 54.554 | 53.329 | -2.25% | passed |
| scheduler-4800-drain | 8.108 | 7.375 | -9.04% | passed |

The initial timing evaluation failed the 1,200-job queue phase at
+17.90%. The production source used by both builds was identical.
In a follow-up pinned to one vCPU, 21 independent samples per revision
alternated the order of reference and candidate executions. The queue
medians were 1.307 and 1.274 ms, a candidate change of −2.52%. Cancellation
and drain passed in that repeat as well. The rerun did not reproduce
the timing regression. Both runs are retained in the evidence.

## Matched preview viewer memory

All seven independent preview processes per revision produced valid
six-cycle outputs. The comparator passed all three preview metrics.
With seven samples, its nearest-rank p95 is also the maximum. All
values in the table are KiB.

| Metric | Reference median | Candidate median | Reference p95 | Candidate p95 | Gate |
|---|---:|---:|---:|---:|---|
| retained-rss | 20448 | 20432 | 20556 | 20572 | passed |
| live-rss | 114252 | 114296 | 114416 | 114360 | passed |
| peak-rss | 114252 | 114296 | 114416 | 114360 | passed |

## Matched tiled viewer memory

Both revisions completed seven independent six-cycle tiled runs with
identical benchmark instrumentation. All eight viewers presented tiles
and finalized in every cycle of every run. The table gives reference
then candidate observations in KiB. The last column counts cycle
observations rejected by the existing consistency check.

| Sample | Retained RSS | Highest sampled live RSS | Last reported VmHWM | Rejected cycles |
|---:|---:|---:|---:|---:|
| 1 | 592080 / 616988 | 738580 / 794868 | 736768 / 792972 | 1 / 2 |
| 2 | 567332 / 650288 | 729936 / 744080 | 729936 / 744080 | 0 / 1 |
| 3 | 715720 / 657244 | 870088 / 751108 | 870088 / 751108 | 0 / 1 |
| 4 | 518664 / 593496 | 751920 / 762932 | 750468 / 761188 | 1 / 1 |
| 5 | 641440 / 535544 | 739356 / 778980 | 739356 / 778980 | 1 / 1 |
| 6 | 498824 / 632136 | 725892 / 766420 | 724320 / 766420 | 1 / 2 |
| 7 | 596828 / 559188 | 690644 / 735380 | 690644 / 733540 | 1 / 2 |

The reference passed the counter validation in 2 of 7 runs. The candidate
passed in 0 of 7. Raw median retained RSS was 592,080 versus 616,988 KiB.
Raw median sampled live RSS was 738,580 versus 762,932 KiB. Those
descriptive comparisons preserve rejected runs. The full release
comparator returned **inconclusive** because both measurement sets lacked
a valid seven-sample tiled workload. No high-water value was substituted
and no valid subset was used to claim a pass.

## Linux high-water counter investigation

Earlier instrumentation sampled live RSS and then read the high-water value
after releasing the viewers. The current benchmark reads `VmRSS` and
`VmHWM` from the same status snapshot at both lifecycle stages and reports
the larger of the two observed `VmHWM` counters for each cycle.

An independent probe on this host allocated and touched a 64 MiB anonymous
mapping, then discarded or unmapped it. Every single-read `VmHWM` was at
least the paired `VmRSS`, but the reported high-water value fell across
reads in both cases. The `MADV_DONTNEED` case fell from 75,300 to
75,284 KiB, and the unmap case fell from 75,304 to 75,300 KiB. This
reproduces a cross-read discrepancy without ImageKit. The running WSL
kernel derives the emitted pair from a shared RSS sum and a separately
maintained high-water counter. Its source documents that the snapshots
can be inconsistent across reads.
[WSL 6.18.58.1 procfs implementation](https://github.com/microsoft/WSL2-Linux-Kernel/blob/linux-msft-wsl-6.18.58.1/fs/proc/task_mmu.c#L34-L68)

## Twenty-cycle tiled viewer diagnosis

The current software-rendered lifecycle diagnostic completed under
Xvfb and Cairo with eight simultaneous viewers. All eight presented
decoded tiles and finalized in every cycle, for 160 viewer releases.
Each live sample counted 24 GSK texture nodes. Each settled sample
recorded zero after viewer release. These counts describe scene nodes.

All memory values below are KiB. Each cell presents the live reading
followed by the settled reading. `smaps_rollup` RSS equaled status RSS
at each diagnostic snapshot. PSS and private dirty are from
`smaps_rollup`. The table preserves the two raw `VmHWM` observations
instead of substituting a peak.

| Cycle | RSS | PSS | Private dirty | VmHWM | Texture nodes |
|---:|---:|---:|---:|---:|---:|
| 1 | 479196 / 459192 | 446278 / 426274 | 390604 / 370600 | 479196 / 477600 | 24 / 0 |
| 2 | 609816 / 609816 | 576898 / 576898 | 521224 / 521224 | 609816 / 609816 | 24 / 0 |
| 3 | 630628 / 630628 | 597707 / 597710 | 542036 / 542036 | 630628 / 630628 | 24 / 0 |
| 4 | 662972 / 605948 | 628720 / 573030 | 574380 / 517356 | 662972 / 661336 | 24 / 0 |
| 5 | 649264 / 649264 | 616346 / 616346 | 560672 / 560672 | 661336 / 661336 | 24 / 0 |
| 6 | 738240 / 681856 | 704000 / 648938 | 649648 / 593264 | 738240 / 736316 | 24 / 0 |
| 7 | 650168 / 617780 | 615908 / 584862 | 561576 / 529188 | 736316 / 736316 | 24 / 0 |
| 8 | 778964 / 697752 | 744704 / 664834 | 690372 / 609160 | 778964 / 776960 | 24 / 0 |
| 9 | 734160 / 734160 | 699914 / 701242 | 645568 / 645568 | 776960 / 776960 | 24 / 0 |
| 10 | 830724 / 700840 | 796538 / 667922 | 742132 / 612248 | 830724 / 828528 | 24 / 0 |
| 11 | 717972 / 717972 | 683708 / 685054 | 629380 / 629380 | 828528 / 828528 | 24 / 0 |
| 12 | 729484 / 729484 | 695226 / 696566 | 640892 / 640892 | 828528 / 828528 | 24 / 0 |
| 13 | 762404 / 762404 | 728159 / 729486 | 673812 / 673812 | 828528 / 828528 | 24 / 0 |
| 14 | 709296 / 660528 | 675040 / 627610 | 620704 / 571936 | 828528 / 828528 | 24 / 0 |
| 15 | 718140 / 636320 | 683887 / 603399 | 629548 / 547728 | 828528 / 828528 | 24 / 0 |
| 16 | 790604 / 611908 | 756369 / 578990 | 702012 / 523316 | 828528 / 828528 | 24 / 0 |
| 17 | 730532 / 637740 | 696324 / 604822 | 641940 / 549148 | 828528 / 828528 | 24 / 0 |
| 18 | 690628 / 645988 | 656383 / 613070 | 602036 / 557396 | 828528 / 828528 | 24 / 0 |
| 19 | 626112 / 626112 | 591865 / 593194 | 537520 / 537520 | 828528 / 828528 | 24 / 0 |
| 20 | 747848 / 638536 | 713621 / 605618 | 659256 / 549944 | 828528 / 828528 | 24 / 0 |

| Snapshot | RSS | PSS | Private dirty | VmHWM | File RSS | Swap |
|---|---:|---:|---:|---:|---:|---:|
| Initial diagnostic | 93912 | 66187 | 21284 | 93912 | 72628 | 0 |
| Final diagnostic | 638536 | 605618 | 549944 | 828528 | 78748 | 0 |

The lightweight lifecycle counter read initial RSS at 93,904 KiB and
final RSS at 638,536 KiB, a retained increase of 544,632 KiB. Settled
RSS climbed during the first half, then fluctuated at an elevated level.
The median fell from 697,752 KiB in cycles 6–10 to 637,740 KiB in
cycles 16–20. The last five settled readings ranged from 611,908 to
645,988 KiB. Their sequence shows no continuing monotonic growth
over the observed window. Anonymous RSS tracked private dirty memory
and file RSS stayed at 78,748 KiB after the first load. These counters
identify the type of residency. Allocation ownership requires an
allocator-level measurement.

The raw high-water value fell between live and settled reads in cycles
1, 4, 6, 8, and 10. The maximum observed raw value was 830,724 KiB at
live cycle 10, while the final reported value was 828,528 KiB. The
diagnostic labeled high-water evidence inconsistent. The earlier
Linux-only reproduction independently demonstrated the same kind of
cross-read decrease.

## Allocator attribution

A separate six-cycle tiled run used a temporary diagnostic probe that
recorded glibc `malloc_info` at the benchmark's existing
`smaps_rollup` reads. It called `malloc_trim(0)` only after saving
the final unmodified diagnostic observation. This probe was excluded
from the timed comparison.

| Allocator or residency counter | Initial | Final before trim | Final after trim |
|---|---:|---:|---:|
| Arena bytes from system | 35303424 | 632578048 | 603381760 |
| Free non-fast arena bytes | 4833383 | 600091716 | 570900562 |
| Arena count | 37 | 56 | 56 |
| Host RSS (KiB) | 93736 | 693452 | 603640 |
| Private dirty (KiB) | 21236 | 604856 | 515044 |

At the final pre-trim sample, glibc reported about 600 MB of free
non-fast chunks within about 633 MB obtained for its arenas. The trim
released 89,812 KiB of both RSS and private dirty memory. This identifies
reclaimable glibc pages directly and attributes much of the remaining
retention to free space held in the arenas. The probe does not identify
the call stacks that created the arena demand or assign every resident
page to a specific allocation. Arena bytes minus reported free chunks,
plus glibc's direct mappings, increased from 31,830,233 to 34,029,484 bytes.
That difference includes allocator overhead and is an estimate rather
than a census of live objects. The unmodified twenty-cycle run and
the allocator probe support investigating allocation churn and arena
retention before changing viewer ownership or cache behavior.

## Physical GPU coverage

**GPU residency and release were not measured.** The default WSLg EGL
probe reported llvmpipe. Forcing the Mesa D3D12 adapter in the Wayland
session reported an NVIDIA GeForce RTX 4060 Ti. The included profiler
then ran the tiled workload for six cycles with this adapter selection.
The benchmark exited successfully and released all eight viewers in
each cycle. Live scene snapshots contained 24, 23, 24, 24, 24, and 24
texture nodes. These are GSK node counts.

The profiler recorded 569 host-memory samples and zero attributable GPU
samples on the D3D12 attempt. WSL exposed vgem DRM nodes, and the NVIDIA
compute-process fallback provided no usable graphics-process counter.
The default-renderer attempt likewise recorded zero GPU samples. Keep
the GPU verdict open until the profiler can read process-attributed
counters on the target desktop.
[Mesa D3D12 adapter selection](https://docs.mesa3d.org/drivers/d3d12.html)
[NVIDIA WSL telemetry limitations](https://docs.nvidia.com/cuda/wsl-user-guide/#features-not-yet-supported)

## Recommended corrections and follow-up

1. Keep Meson's test dependency rebuild in the qualification runner so
   fresh builds generate the same fixtures used by integration tests.
2. Repeat the tiled peak-memory qualification on the target native Debian
   or Majoris kernel. Preserve paired raw RSS and peak readings. If the
   target also produces an inconsistent peak, validate a separate peak
   metric and its comparability before changing the gate or using its
   values in a release decision.
3. Use an alternating, CPU-controlled schedule if the 1,200-job queue
   anomaly recurs. Preserve the original threshold and sample records.
4. Use a heap profiler with allocation stacks if longer runs show renewed
   growth in bytes still in use. The present evidence does not warrant
   changes to viewer cache or ownership behavior.
5. Run the GPU profiler on a target desktop that exposes per-process
   graphics memory attribution throughout texture load and release.

## Raw evidence

Build logs, both Meson suites, the archive runner result, the diagnostic log
and JSON, and the reference/candidate sample files are stored in
`~/majoris-verification/final-qualification/` in the Debian distribution.
Key files include `matched-result.json`, `timing-comparison.json`,
`scheduler-rerun.json`, `preview-memory-comparison.json`,
`matched-viewer-raw.json`, `comparison.json`, `viewer-diagnostic.json`,
`gpu-d3d12.json`, and `raw-preflight.json`. The `allocator/` directory
contains the glibc heap XML and trim log. Both profiles retain full Meson
benchmark logs.
