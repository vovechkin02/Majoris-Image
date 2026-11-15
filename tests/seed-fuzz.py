"""Build a reusable corpus for the native parser harness."""

import argparse
from pathlib import Path
import struct
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--profile-png", type=Path, help="Add the generated ICC profile")
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent / "fixtures"
    for name, source, selector in [
        ("thumbnail", "rgb16.png", 0),
        ("heif", "heic-sequence.heic", 2),
        ("avif", "avif-sequence.avif", 3),
    ]:
        (args.directory / name).write_bytes(bytes([selector]) + (fixtures / source).read_bytes())
    (args.directory / "float-rgba").write_bytes(bytes([4]) + b"MIF1" + struct.pack(">II", 1, 1) + bytes(16))
    if args.profile_png:
        data = args.profile_png.read_bytes()
        offset = 8
        while offset < len(data):
            size, kind = struct.unpack_from(">I4s", data, offset)
            payload = data[offset + 8:offset + 8 + size]
            if kind == b"iCCP":
                encoded = payload.split(bytes(1), 1)[1][1:]
                (args.directory / "icc").write_bytes(bytes([1]) + zlib.decompress(encoded))
                break
            offset += 12 + size
        else:
            parser.error("the supplied PNG has no embedded ICC profile")
    else:
        (args.directory / "icc-truncated").write_bytes(bytes([1]) + b"invalid-profile")


if __name__ == "__main__":
    main()
