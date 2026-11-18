# Files integration: lossless JPEG export verification

18 November 2026 (Australia/Sydney). Base main revision:
`5428aa1da43c8d649cfbad79a54a83c7e18831ea`. Local working version: 2.0.1.

The Files migration exposed a missing public lossless JPEG quarter-turn export.
The existing ExportOptions.lossless flag now selects a perfect coefficient
transform for JPEG inside the isolated worker. Source pinning, identity checks,
private output validation and transactional publication remain in EncoderRegistry.
No coefficient processing remains in Majoris-Files.

The worker composes EXIF orientation with RenderOptions.turns, retains metadata
and profiles, normalizes orientation and removes stale embedded EXIF thumbnails.
Imperfect edge transforms and non-quarter-turn edits fail with UNSUPPORTED;
there is no trimming or silent re-encode. See docs/api-2.md for the contract.

Verification:

- Native lossless-jpeg.c passes -Wall -Wextra -Werror independently.
- Public lossless JPEG export test passes with asymmetric image content, all
  eight EXIF orientations compared with the independent pixel renderer, exact
  decoded pixels after a clockwise/counterclockwise coefficient round trip,
  imperfect-edge refusal, extra-edit refusal, cancellation and source protection.
- The public test also compiles and passes against the installed stock Debian
  runtime, without a worker override.
- Core, GTK, security and installed-consumer suites were exercised by the Files
  integration. The existing nine-camera RAW corpus also passed qualification.
- Five stock Debian 2.0.1 packages built and installed. Linux packaging file modes
  were restored from recorded Git modes after export from Windows.
- Installed C, Vala, GtkBuilder, GI, Photos export and thumbnail consumers pass
  using the configured production worker path.

These are local uncommitted upstream changes, not a published release. A monitored
maintainer address is still required: the original packages@majoris.invalid was
preserved. The existing generated-C/XTest repository-wide strict-warning gate and
physical HDR/Wayland/GNOME release qualifications are not resolved by this change.
The Files repository records the exact working-file SHA256 manifest and complete
integration results in docs/imagekit-revision.json and docs/imagekit-integration.md.
