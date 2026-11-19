"""Create reproducible 10/50/100-frame APNGs without third-party Python modules."""

import argparse
import binascii
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
WIDTH = 96
HEIGHT = 64


def chunk(kind: bytes, data: bytes) -> bytes:
    """Encode a length-delimited PNG chunk with its CRC-32 checksum."""
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)
    )


def frame_pixels(index: int) -> bytes:
    """Construct a deterministic, unfiltered RGBA frame with changing pixels."""
    rows = bytearray()
    for y in range(HEIGHT):
        rows.append(0)
        for x in range(WIDTH):
            rows.extend(((x * 3 + index * 13) & 255,
                         (y * 7 + index * 5) & 255,
                         (x ^ y ^ index * 11) & 255, 255))
    return zlib.compress(rows, 6)


def make_apng(path: Path, frame_count: int) -> None:
    """Write full-frame APNG source-blend frames with distinct frame values."""
    if frame_count not in (10, 50, 100):
        raise ValueError("Supported qualification frame counts are 10, 50 and 100")
    chunks = [
        chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)),
        chunk(b"sRGB", b"\x00"),
        chunk(b"acTL", struct.pack(">II", frame_count, 1)),
    ]
    sequence = 0
    for frame in range(frame_count):
        # APNG sequence numbers cover fcTL and fdAT, not IDAT.
        control = struct.pack(
            ">IIIIIHHBB", sequence, WIDTH, HEIGHT, 0, 0, 4, 100, 0, 0
        )
        chunks.append(chunk(b"fcTL", control))
        sequence += 1
        encoded = frame_pixels(frame)
        if frame == 0:
            chunks.append(chunk(b"IDAT", encoded))
        else:
            chunks.append(chunk(b"fdAT", struct.pack(">I", sequence) + encoded))
            sequence += 1
    chunks.append(chunk(b"IEND", b""))
    path.write_bytes(PNG_SIGNATURE + b"".join(chunks))


def main() -> int:
    """Write the three deterministic Meson output files in frame-count order."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outputs", type=Path, nargs=3)
    args = parser.parse_args()
    for count, path in zip((10, 50, 100), args.outputs, strict=True):
        make_apng(path, count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
