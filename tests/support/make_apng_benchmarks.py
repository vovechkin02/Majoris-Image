"""Create long-frame and composited APNG benchmarks without external modules."""

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


def frame_pixels(index: int, width: int = WIDTH, height: int = HEIGHT,
                 alpha: bool = False) -> bytes:
    """Construct a deterministic, unfiltered RGBA frame with changing pixels."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(((x * 3 + index * 13) & 255,
                         (y * 7 + index * 5) & 255,
                         (x ^ y ^ index * 11) & 255,
                         64 + (x + y + index * 7) % 192 if alpha else 255))
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


def make_composited_apng(path: Path) -> None:
    """Build a larger APNG with alpha-over, subrectangles and dispose operations."""
    width, height, count = 512, 384, 24
    chunks = [
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        chunk(b"sRGB", b"\x00"),
        chunk(b"acTL", struct.pack(">II", count, 1)),
    ]
    sequence = 0
    for frame in range(count):
        region_width = width if frame == 0 else 192
        region_height = height if frame == 0 else 128
        offset_x = 0 if frame == 0 else frame * 23 % (width - region_width)
        offset_y = 0 if frame == 0 else frame * 17 % (height - region_height)
        disposal = 0 if frame == 0 else frame % 3
        blend = 0 if frame == 0 else 1
        control = struct.pack(
            ">IIIIIHHBB", sequence, region_width, region_height,
            offset_x, offset_y, 7, 100, disposal, blend,
        )
        chunks.append(chunk(b"fcTL", control))
        sequence += 1
        encoded = frame_pixels(frame, region_width, region_height, frame != 0)
        if frame == 0:
            chunks.append(chunk(b"IDAT", encoded))
        else:
            chunks.append(chunk(b"fdAT", struct.pack(">I", sequence) + encoded))
            sequence += 1
    chunks.append(chunk(b"IEND", b""))
    path.write_bytes(PNG_SIGNATURE + b"".join(chunks))


def main() -> int:
    """Write three scaling APNGs and one larger compositing fixture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outputs", type=Path, nargs=4)
    args = parser.parse_args()
    for count, path in zip((10, 50, 100), args.outputs[:3], strict=True):
        make_apng(path, count)
    make_composited_apng(args.outputs[3])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
