"""Generate signature-driven legacy and animated fixtures with no external inputs."""

import argparse
import binascii
import struct
import zlib
from pathlib import Path


def chunk(kind: bytes, payload: bytes = b"") -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", binascii.crc32(kind + payload))
    )


def rgba(pixels: list[int]) -> bytes:
    return zlib.compress(b"\0" + bytes(pixels))


def write_partial_fixtures(qoi: Path, apng: Path) -> None:
    # Two independently encoded pixels with different transparency.
    qoi.write_bytes(
        b"qoif"
        + struct.pack(">IIBB", 2, 1, 4, 0)
        + b"\xff\xff\0\0\x80\xff\0\xff\0\xff"
        + b"\0" * 7
        + b"\1"
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


def write_orientation_fixtures(oriented: list[Path]) -> None:
    # An asymmetric 3x2 TIFF with independently identifiable corners.
    pixels = bytes(
        [255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 0, 255, 255, 255, 0, 255]
    )
    for value, path in enumerate(oriented, 1):
        tags = [
            (256, 4, 1, 3),
            (257, 4, 1, 2),
            (258, 3, 3, 0),
            (259, 3, 1, 1),
            (262, 3, 1, 2),
            (273, 4, 1, 0),
            (274, 3, 1, value),
            (277, 3, 1, 3),
            (278, 4, 1, 2),
            (279, 4, 1, len(pixels)),
            (284, 3, 1, 1),
        ]
        bit_table = 8 + 2 + len(tags) * 12 + 4
        tags[2] = (258, 3, 3, bit_table)
        tags[5] = (273, 4, 1, bit_table + 6)
        path.write_bytes(
            bytes((73, 73, 42, 0))
            + struct.pack("<IH", 8, len(tags))
            + b"".join(struct.pack("<HHII", *tag) for tag in tags)
            + struct.pack("<IHHH", 0, 8, 8, 8)
            + pixels
        )


def write_legacy_fixtures(
    bmp: Path, ico: Path, tga: Path, dds: Path, pgm: Path
) -> None:
    # Independently encoded 2x1 red and green pixels, plus a grayscale PGM.
    bmp_pixels = bytes((0, 0, 255, 0, 255, 0, 0, 0))
    bmp_info = struct.pack(
        "<IiiHHIIiiII", 40, 2, 1, 1, 24, 0, len(bmp_pixels), 0, 0, 0, 0
    )
    bmp.write_bytes(b"BM" + struct.pack("<IHHI", 62, 0, 0, 54) + bmp_info + bmp_pixels)
    ico_image = (
        struct.pack("<IiiHHIIiiII", 40, 2, 2, 1, 32, 0, 8, 0, 0, 0, 0)
        + bytes((0, 0, 255, 255, 0, 255, 0, 255))
        + bytes(4)
    )
    ico.write_bytes(
        struct.pack("<HHHBBBBHHII", 0, 1, 1, 2, 1, 0, 0, 1, 32, len(ico_image), 22)
        + ico_image
    )
    tga.write_bytes(
        struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 2, 1, 24, 32)
        + bmp_pixels[:6]
        + bytes(8)
        + b"TRUEVISION-XFILE.\0"
    )
    # DDS DXT1 block with a red and a green endpoint, alternating selectors.
    dds_header = [124, 0x81007, 4, 4, 8, 0, 0] + [0] * 11
    dds_pixel_format = [32, 4, int.from_bytes(b"DXT1", "little"), 0, 0, 0, 0, 0]
    dds.write_bytes(
        b"DDS "
        + struct.pack("<31I", *(dds_header + dds_pixel_format + [0x1000, 0, 0, 0, 0]))
        + struct.pack("<HHI", 0xF800, 0x07E0, 0x44444444)
    )
    pgm.write_bytes(b"P5\n2 1\n255\n" + bytes((64, 255)))


def write_color_fixtures(bad_icc: Path, srgb: Path, cicp: Path) -> None:
    bad_icc.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0))
        + chunk(b"iCCP", b"Invalid\0\0" + zlib.compress(b"this is not an ICC profile"))
        + chunk(b"IDAT", rgba([255, 0, 0, 255] * 2))
        + chunk(b"IEND")
    )

    for target, declaration in (
        (srgb, chunk(b"sRGB", bytes(1))),
        (cicp, chunk(b"cICP", bytes((9, 16, 0, 1)))),
    ):
        target.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0))
            + declaration
            + chunk(b"IDAT", rgba([128, 64, 32, 128] * 2))
            + chunk(b"IEND")
        )


def write_heic_fixtures(
    indefinite: Path,
    offset_edit: Path,
    invalid_timing: Path,
    rate_edit: Path,
    composition: Path,
    trimmed: Path,
) -> bytes:
    # Mutations retain the original sample data and all chunk offsets. The fixture
    # uses version-zero movie, track and edit headers with one whole-track repeat.
    heic = (
        Path(__file__).resolve().parents[1] / "fixtures" / "heic-sequence.heic"
    ).read_bytes()

    def replace_field(box, offset, value):
        position = heic.index(box) + offset
        return heic[:position] + struct.pack(">I", value) + heic[position + 4 :]

    indefinite.write_bytes(replace_field(b"mvhd", 20, 0xFFFFFFFF))
    offset_edit.write_bytes(replace_field(b"elst", 16, 1))
    invalid_timing.write_bytes(replace_field(b"stts", 16, 0))
    rate_edit.write_bytes(replace_field(b"elst", 20, 0x20000))

    # Insert a nonzero composition offset into the selected track's sample table.
    # Relocate the stored offsets for the media bytes after the expanded movie box.
    composed = bytearray(heic)
    sample_table = heic.index(b"stbl") - 4
    insert_at = sample_table + struct.unpack_from(">I", heic, sample_table)[0]
    ctts = struct.pack(">I4sIIII", 24, b"ctts", 0, 1, 2, 1)
    for box in (b"stbl", b"minf", b"mdia", b"trak", b"moov"):
        size_at = heic.index(box) - 4
        size = struct.unpack_from(">I", heic, size_at)[0]
        struct.pack_into(">I", composed, size_at, size + len(ctts))
    offset_table = heic.index(b"stco")
    for i in range(struct.unpack_from(">I", heic, offset_table + 8)[0]):
        field = offset_table + 12 + i * 4
        value = struct.unpack_from(">I", heic, field)[0]
        struct.pack_into(">I", composed, field, value + len(ctts))
    composed[insert_at:insert_at] = ctts
    composition.write_bytes(composed)

    # A 20 ms empty edit followed by part of the red picture and 60 ms of green.
    edited = bytearray(heic)
    entry = heic.index(b"elst")
    old_size = struct.unpack_from(">I", heic, entry - 4)[0]
    edits = ((20, -1), (35, 35), (60, 70))
    new_box = struct.pack(">I4sII", 16 + 12 * len(edits), b"elst", 0, len(edits))
    new_box += b"".join(
        struct.pack(">IiI", length, start, 0x10000) for length, start in edits
    )
    delta = len(new_box) - old_size
    for parent in (b"edts", b"trak", b"moov"):
        pos = heic.index(parent) - 4
        struct.pack_into(
            ">I", edited, pos, struct.unpack_from(">I", heic, pos)[0] + delta
        )
    for tag in (b"tkhd", b"mvhd"):
        struct.pack_into(
            ">I", edited, heic.index(tag) + 20, sum(length for length, _ in edits)
        )
    for i in range(struct.unpack_from(">I", heic, offset_table + 8)[0]):
        field = offset_table + 12 + 4 * i
        struct.pack_into(
            ">I", edited, field, struct.unpack_from(">I", heic, field)[0] + delta
        )
    edited[entry - 4 : entry - 4 + old_size] = new_box
    trimmed.write_bytes(edited)
    return heic


def write_linear_qoi(qoi_linear: Path) -> None:
    qoi_linear.write_bytes(
        b"qoif"
        + struct.pack(">IIBB", 2, 1, 4, 1)
        + bytes((255, 128, 64, 32, 128, 255, 32, 64, 128, 0))
        + bytes(7)
        + bytes((1,))
    )


def write_fractional_heic(heic: bytes, partial_repeat: Path) -> None:
    # Play one complete 230 ms cycle and the first 40 ms of the next.
    fractional = bytearray(heic)
    for tag in (b"tkhd", b"mvhd"):
        struct.pack_into(">I", fractional, heic.index(tag) + 20, 270)
    partial_repeat.write_bytes(fractional)


def write_profiled_animation(profiled: Path, apng: Path, wide_apng: Path) -> None:
    # The native fixture maker supplies a real Display P3 profile.
    profile_png = profiled.read_bytes()
    offset = 8
    while offset + 12 <= len(profile_png):
        length, kind = struct.unpack_from(">I4s", profile_png, offset)
        end = offset + length + 12
        if kind == b"iCCP":
            source = apng.read_bytes()
            wide_apng.write_bytes(source[:33] + profile_png[offset:end] + source[33:])
            break
        offset = end
    else:
        raise ValueError("Display P3 fixture has no embedded profile")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiled", type=Path)
    parser.add_argument("outputs", nargs=27, type=Path)
    args = parser.parse_args()
    profiled, qoi, apng, *formats = args.profiled, *args.outputs
    oriented = formats[:8]
    (
        bmp,
        ico,
        tga,
        dds,
        pgm,
        bad_icc,
        srgb,
        cicp,
        indefinite,
        offset_edit,
        invalid_timing,
        rate_edit,
        composition,
        trimmed,
        qoi_linear,
        partial_repeat,
        wide_apng,
    ) = formats[8:]

    write_partial_fixtures(qoi, apng)
    write_orientation_fixtures(oriented)
    write_legacy_fixtures(bmp, ico, tga, dds, pgm)
    write_color_fixtures(bad_icc, srgb, cicp)
    heic = write_heic_fixtures(
        indefinite, offset_edit, invalid_timing, rate_edit, composition, trimmed
    )
    write_linear_qoi(qoi_linear)
    write_fractional_heic(heic, partial_repeat)
    write_profiled_animation(profiled, apng, wide_apng)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
