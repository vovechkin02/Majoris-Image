"""Validate generated desktop metadata against the public thumbnail command."""
import configparser
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

thumbnailer, registration, source_fixtures, generated = map(Path, sys.argv[1:])
thumbnailer = thumbnailer.resolve()
source_fixtures = source_fixtures.resolve()
generated = generated.resolve()
config = configparser.ConfigParser(interpolation=None)
config.read(registration)
desktop = config["Thumbnailer Entry"]
assert desktop["Exec"].endswith(" %u %o %s")
assert desktop["Exec"].startswith(desktop["TryExec"])
mimes = desktop["MimeType"].rstrip(";").split(";")
assert len(mimes) == len(set(mimes))
assert {"image/jpeg", "image/png", "image/tiff", "image/x-exr"} <= set(mimes)

cases = [
    ("image/png", source_fixtures / "rgb16.png", (24, 16)),
    ("image/tiff", generated / "orientation-6.tiff", (2, 3)),
    ("image/x-exr", source_fixtures / "multipart.exr", (32, 16)),
    ("image/svg+xml", source_fixtures / "vector.svg", None),
    ("image/gif", generated / "animated.gif", (8, 8)),
    ("image/webp", generated / "animated.webp", (8, 8)),
    ("image/jp2", generated / "jpeg2000.jp2", (8, 8)),
    ("image/apng", generated / "partial.apng", (2, 1)),
    ("image/qoi", generated / "fallback.qoi", (2, 1)),
]
with tempfile.TemporaryDirectory(prefix="majoris-registration-test-") as tmp:
    out = Path(tmp) / "out.png"
    for mime, fixture, size in cases:
        if mime not in mimes:
            print(f"{mime} omitted by this build")
            continue
        if mime == "image/jp2" and fixture.stat().st_size == 0:
            print("JPEG 2000 sample generation unavailable in this build")
            continue
        subprocess.run(
            [str(thumbnailer), fixture.as_uri(), str(out), "48"],
            env=dict(os.environ, XDG_CACHE_HOME=tmp), check=True,
        )
        png = out.read_bytes()
        assert png.startswith(b"\x89PNG\r\n\x1a\n") and b"Thumb::URI" in png
        width, height, depth = struct.unpack_from(">IIB", png, 16)
        assert 0 < width <= 48 and 0 < height <= 48 and depth == 8
        if size:
            assert (width, height) == size, (mime, width, height, size)
        if mime in ("image/qoi", "image/x-exr"):
            assert png[25] == 6  # RGB with alpha
        print(f"{mime}: {width}x{height} SDR thumbnail")
