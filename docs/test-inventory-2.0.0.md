# Baseline test inventory

This historical inventory retains the names and paths recorded at the time.
See [current test commands](TESTING.md) for the sentence-case registrations.

The original tests ran by default with testing enabled unless a row says otherwise.
Meson bounded every command (30 seconds by default). The source tree supplied
small fixtures and Meson generated larger and codec-specific inputs. No row
required physical hardware. Worker-facing rows shared one mutable XDG cache
unless noted.

| Meson test | Implementation / type | Fixtures and dependencies | Baseline assertions and gaps |
| --- | --- | --- | --- |
| OpenEXR regions | test-exr.cpp / native | Generated multipart EXR, OpenEXR, libvips | Float pixels, windows, parts, primaries, and regions. |
| Imaging references | test-imaging.c / native | Generated ICC, EXR, TIFF, JXL, SVG, HEIF, optional Ultra HDR | Numeric transfer and color references, round trips, codec-dependent gain maps. |
| Foreground, tile and preload scheduling | scheduler.vala / unit | GLib, Gee | Saturation, priority, cancellation, and acquire/release order. |
| Public headless API | headless.vala / integration | Sandboxed worker, PNG, SVG, EXR, generated 200 MP TIFF | Dimensions, alpha, region shape, tone mapping, cache and preload identity. Thumbnail only checked the PNG signature. |
| Decoder selection and composited APNG frames | decoders.vala / integration | Worker, fault helper, generated formats, optional Glycin and timed sequences | Frame colors, timing, strict color behavior and fallback. Several format variants only checked identity or dimensions. |
| Desktop thumbnail registration and sample output | thumbnail-registration.py / integration | Worker, registration generator, generated fixtures, optional codec plugins | MIME and desktop records, PNG header, dimensions and metadata marker. No pixel decode. |
| Cross-process thumbnail publication | cache-processes.py / extended | Worker, Bubblewrap, PNG, private shared cache | Worker counts, byte equality, corrupt header recovery, crash and file mutation. Delays staged races, and no CRC-valid compression corruption was tested. |
| Real camera RAW | raw-samples.vala / interoperability | External RAW directory, LibRaw, worker | Preview, development, metadata, white balance, and input checksum. Registered only with a configured directory and accepted any nonempty sample subset. |
| Worker timeout, cancellation, crash and response validation | worker-security.vala, worker-fault.c / security | Fault worker and Bubblewrap | Error domains, read-only source, negative dimensions. No direct network or host mount test. |
| HDR presentation policy | presentation.vala / unit, GTK linkage | Synthetic presentations, GTK4, no display | Policy and result status mapping. No physical HDR output. |
| GDK floating-point texture color state | image-texture.c / unit, GTK linkage | Synthetic pixels, GTK4, no display | Float layout, texture identity and color state. |
| GTK input and accessibility at scales 1 and 2 | widget-interaction.c / GTK integration | Xvfb, XTest, GTK X11 | Mouse/keyboard interaction, scroll handling and accessibility. Tests were absent without those dependencies. |
| Wide viewport tile coverage and cancellation | tile-coverage.vala / GTK integration | GTK texture API, no display, generated TIFF and EXR | Geometry, cancellation, and one numerical HDR tile. The complete 160-tile case was only reachable with a manual flag. |
| Files public preview workflow | files-preview.vala / GTK integration | GTK display, worker, PNG, EXR, generated TIFF and optional RAW/sequences | Async replacement, fit, thumbnail and presentation behavior. |
| Photos public full-quality workflow | photos-viewer.vala / GTK integration | GTK display, worker, generated TIFF/JPEG/QOI, EXR and optional RAW/sequences | Navigation, region shape and export reopen. Fixed export path and no export pixel assertion. |
| Public GTK Builder API | builder.c / GTK integration | GTK display, worker, PNG | XML, accessibility, async open, region and thumbnail calls. |
| GTK view lifecycle and navigation | viewer.vala / GTK integration | GTK display, PNG, TIFF, EXR, GIF/WebP, optional Glycin/sequences | State, page/frame navigation, cancellation and disposal. Some waits depended on the outer process timeout. |

The independently invoked installed shell workflow was outside Meson. It skipped
Photos whenever the generated TIFF was absent from the source fixture directory
and counted an idle demo surviving three seconds as success. The benchmark
binary was compiled without a registered benchmark.
