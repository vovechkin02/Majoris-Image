# Majoris Image architecture (ABI 2)

**Status:** Current design reference for Majoris Image 2.0.1. This document describes
implementation boundaries and accepted design decisions, not additional public API.
Refer to [the API guide](api-2.md) for consumer contracts, and to
[historical verification](verification/README.md) for prior test results.

## Component and dependency map

```text
Host application (Majoris Photos / Files / other consumer)
  ├── ImageKitGtk.ImageView (src/gtk/; GTK4/GDK)
  │     ├── TileCache, AnimationController, Presentation
  │     └── ImageKit.ImageDocument / DecoderRegistry
  └── ImageKit Core (src/core/; headless GObject/GIO)
        ├── Decoding selection, source validation, Glycin fallback
        ├── ImageSource / ImageDocument / PixelBuffer / options
        ├── WorkerClient and bounded worker-output reader
        ├── EncoderRegistry and export publication
        ├── ThumbnailService / PreloadManager / JobScheduler
        └── Bubblewrap process boundary → worker (src/worker/)
                                             └── native codec bridge (native/)
```

`ImageKitGtk` depends on `ImageKit` Core, never the reverse. Core holds no GTK
requirement; disabled GTK builds remain valid. The worker is an independent
executable and parses untrusted image content inside Bubblewrap, not in the
consumer process. `native/` supplies the worker's C/C++ codec functionality.
The worker contract is protocol version 1; moving source files does not change
its messages or resource limits.

## Source directories

| Path | Responsibility | Boundary |
| --- | --- | --- |
| `src/core/` | Source/document/pixel models, recipes, profiling and preload coordination | Public headless contract and cohesive shared helpers |
| `src/core/decoding/` | Decoder selection, capabilities, probe, sniffing and optional Glycin | Recognize and normalize format-specific behavior |
| `src/core/execution/` | Worker IPC, job scheduling, output validation and worker contract records | Subprocess, cancellation, sandbox and result lifetime |
| `src/core/export/` | Export request, protected source, bounded transfer and destination publication | Atomic and remote publication policies |
| `src/core/thumbnail/` | Thumbnail generation, file validation and private cache | Bounded PNG outputs |
| `src/worker/` | Process entry point, operation dispatcher and format processing | Untrusted data processing in sandbox |
| `native/` | C/C++ codec, color, metadata and pixel adapters | Native library interoperability |
| `src/gtk/` | ImageView, animation, tile cache, presentation and private rendering support | GTK lifecycle, input and display presentation |
| `src/thumbnail/` | Desktop thumbnailer executable and registration | Desktop integration |
| `tests/` | Unit, security, integration, benchmark, release and support fixtures | Independent verification |
| `docs/verification/` | Historical, date- and milestone-specific evidence | Never the canonical API or architecture contract |

One authoritative `src/core/meson.build` registers all Core and shared worker
contract sources. Source files referenced by tests and the worker are not copied
into new declarations or parallel source lists; the build uses the same files.
The GTK build intentionally compiles a private copy of `decoder-policy.vala` to
avoid exposing the internal `DecoderRoute` type through the installed Core ABI.

## Public declarations and type placement

`src/core/decoding/decoder-types.vala` owns the public `ImageCapabilities`
flags and private `DecodeFailure` classification. The public `RawState`
declaration lives next to its probe representation in
`src/core/decoding/image-probe.vala`. The `DecoderBackend` interface has its
own file, `src/core/decoding/decoder-backend.vala`.

`src/gtk/image-view-types.vala` owns `ViewState`, `FitMode`, and
`ScrollZoomMode`. `src/gtk/presentation-types.vala` owns `PresentationPolicy`
and `PresentationStatus`. `ImageView` remains the implementation in
`src/gtk/image-view.vala`; the names, numeric enum values, GObject properties,
signals and API 2 GIR namespace remain unchanged. These splits do not create
new classes, objects, resource lifecycles or virtual calls.

## GoF pattern decisions

| GoF pattern or principle | Project participants | Intent / ownership |
| --- | --- | --- |
| **Strategy** | `DecoderBackend.accepts()`, `DecoderRegistry` | Vary registered worker acceptance and selection through an interface |
| **Facade** | `DecoderRegistry`, `ImageDocument`, `EncoderRegistry`, `ImageView` | Stable host-facing operations hide worker, codec and presentation details |
| **Observer** | GObject notifications/signals, `TileCache.changed` | Invalidate and redraw from events, not polling |
| **Singleton** | Internal `JobScheduler.shared()` | One bounded job admission policy; not a generic service locator |
| **Adapter-like integration** | Glycin fallback and native bridge | Normalize unrelated dependency APIs into existing image types; not a formal object Adapter |
| **Proxy-like boundary** | `WorkerClient` | Mediate sandbox requests without claiming a full GoF Proxy API |
| **State machine** | `ViewState`, animation generation counters | Explicit state and cancellation; not a polymorphic GoF State hierarchy |
| **Typed routing policy** | `DecoderRoute`, `WorkerLoaderPolicy` | Centralize backend/loader identity tests; not separately a GoF Strategy |

**Composition over inheritance:** `ImageView` composes TileCache,
AnimationController and Presentation. ImageDocument composes the image source,
probe, preview and worker. Do not replace these with subtype families merely
to match a pattern diagram.

**Accepted, bounded exceptions:** Glycin is a fallback path rather than an
implementation of the registered-worker Strategy execution interface. Some
GTK presentation choices depend on the private fallback route. No new
execution-provider interface is justified unless additional implementations
require true substitutability and a compatibility-preserving design is proven.
The worker's finite protocol operations remain explicit dispatch rather than
a Command-object hierarchy. Keep these decisions explicit; do not advertise
them as fully implemented GoF patterns.

## Safety, ownership and performance invariants

- Keep public API 2, SONAME 2, GIR/VAPI and protocol 1 intact.
- Core processing and worker parsing remain independent of GTK.
- Worker output is validated through a stable descriptor and bounded reads.
- Native export protects source identity and publication; non-native export
  retains the verified complete-buffer compatibility path.
- Cancellation and generation checks prevent stale results from replacing
  the active document or presentation.
- Tile resources, preloads and worker jobs retain their documented budgets.
- Maintain asynchronous I/O and avoid unnecessary allocations or new virtual
  dispatch in pixel, frame and tile hot paths.
- Some metadata values have legacy wire spellings (`colour-managed`); never
  rename them while preserving protocol 1.

## Verification before changing structure

This restructuring is intended to change locations and declaration grouping,
not behavior. Revalidate a configured Meson build, all Core and GTK ABI 2
baselines, generated GIR/VAPI, installed external consumers, worker protocol,
headless builds and the relevant test profiles on the release toolchain.
Historical benchmark figures remain evidence for their recorded revisions,
**not** measurements for this source layout. See [test procedures](TESTING.md)
and [the project README](../README.md).
