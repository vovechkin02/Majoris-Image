"""Generate signature-driven legacy and animated fixtures with no external inputs."""
import binascii
import pathlib
import struct
import sys
import zlib


def chunk(kind, payload=b""):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(
        ">I", binascii.crc32(kind + payload)
    )


def rgba(pixels):
    return zlib.compress(b"\0" + bytes(pixels))


qoi, apng, *oriented = map(pathlib.Path, sys.argv[1:])
# Two independently encoded pixels with different transparency.
qoi.write_bytes(
    b"qoif" + struct.pack(">IIBB", 2, 1, 4, 0)
    + b"\xff\xff\0\0\x80\xff\0\xff\0\xff" + b"\0" * 7 + b"\1"
)
# A full red canvas, a partial 50% green OVER frame disposed to previous,
# and finally a partial replace on the right. Delays are 40, 170 and 250 ms.
first = struct.pack(">IIIIIHHBB", 0, 2, 1, 0, 0, 4, 100, 0, 0)
middle = struct.pack(">IIIIIHHBB", 1, 1, 1, 0, 0, 17, 100, 2, 1)
last = struct.pack(">IIIIIHHBB", 3, 1, 1, 1, 0, 25, 100, 0, 0)
apng.write_bytes(
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0))
    + chunk(b"acTL", struct.pack(">II", 3, 1))
    + chunk(b"fcTL", first)
    + chunk(b"IDAT", rgba([255, 0, 0, 255] * 2))
    + chunk(b"fcTL", middle)
    + chunk(b"fdAT", struct.pack(">I", 2) + rgba([0, 255, 0, 128]))
    + chunk(b"fcTL", last)
    + chunk(b"fdAT", struct.pack(">I", 4) + rgba([0, 0, 255, 0]))
    + chunk(b"IEND")
)


# An asymmetric 3x2 TIFF with independently identifiable corners.
pixels = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255,
                255, 255, 0, 0, 255, 255, 255, 0, 255])
for value, path in enumerate(oriented, 1):
    tags = [
        (256, 4, 1, 3), (257, 4, 1, 2), (258, 3, 3, 0), (259, 3, 1, 1),
        (262, 3, 1, 2), (273, 4, 1, 0), (274, 3, 1, value), (277, 3, 1, 3),
        (278, 4, 1, 2), (279, 4, 1, len(pixels)), (284, 3, 1, 1),
    ]
    bit_table = 8 + 2 + len(tags) * 12 + 4
    tags[2] = (258, 3, 3, bit_table)
    tags[5] = (273, 4, 1, bit_table + 6)
    path.write_bytes(
        bytes((73, 73, 42, 0)) + struct.pack("<IH", 8, len(tags))
        + b"".join(struct.pack("<HHII", *tag) for tag in tags)
        + struct.pack("<IHHH", 0, 8, 8, 8) + pixels
    )
