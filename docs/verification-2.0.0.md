# Majoris-Image 2.0.0 verification

The headless and GTK4 libraries use the `ImageKit` Vala namespace and
`image_kit_` C prefix. The widget registers as `ImageKitView`. Native codec
helpers use `image_codec_`, and native texture helpers use `image_texture_`.
Public properties use American English, including `color_managed`,
`color_diagnostic`, and `nearest_neighbor`.

Both ELF SONAMEs moved to `.so.2`. `readelf` confirmed the old and new SONAMEs
and the GTK library's dependency on the ABI 2 core. A dynamic export
comparison found a corresponding ABI 2 symbol for all 285 core exports
and all 77 GTK4 exports from the stock ABI 1 packages. The generated C
headers and Vala bindings use the new names. The GIR files and typelibs
are `ImageKit-2.0` and `ImageKitGtk-2.0`. See the
[API 2 guide](api-2.md) for migration details.

| Configuration | Result |
| --- | --- |
| GTK4 with Glycin, timed sequences, Ultra HDR, and real camera RAW fixtures | 18 tests passed, including the C GtkBuilder, Files-style, and Photos-style workflows. |
| Headless with Glycin and timed sequences | 8 tests passed. The core's ELF dependencies contain no GTK or GDK library. |
| Stock Debian and gain-map profiles | Each built five amd64 packages and passed 17 tests during packaging. |

In isolated Debian 13.7, both profiles passed the installed C, Vala, and
Python consumers, including GtkBuilder registration, the Files-style
and Photos-style examples, and the desktop thumbnailer. The gain-map
profile also exported and reopened an 8-by-8 JPEG gain map with the vendor
runtime. The build emitted Vala-generated C warnings, and the Xvfb test
environment reported an unavailable session bus.

A legacy worker request and saved recipe still use the `nearest-neighbour`
field. The recipe regression test reads and writes the legacy spelling
through the new API. For the PNG fixture, ABI 1 and ABI 2 workers produced
byte-identical tile pixels for the same nearest-neighbor request. Changing
that setting changed the pixels. The manifest keys, color-probe response,
and malformed-request diagnostic matched across both workers.

Remaining matches in the naming audit are intentional. Product and
distribution names cover installed commands, package IDs, paths, desktop
registration, and environment variables such as `MAJORIS_IMAGE_WORKER`.
Dependency spellings include `vips_colourspace`, `VIPS_INTERPRETATION_GREY16`,
and GLib's `CANCELLED` and cancellation APIs. Existing worker, recipe, and
image metadata keys retain their compatibility spellings, including
`colour-managed`, `colour-diagnostic`, and `colour-probe`. Historical
verification reports quote the earlier API where applicable. The official
license text and copyright notices retain their original wording.
