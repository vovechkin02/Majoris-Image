"""Exercise cache and worker behavior across independent thumbnailer processes."""

import argparse
import os
import shutil
import struct
import subprocess
import tempfile
import time
import zlib
from contextlib import ExitStack
from pathlib import Path


def pixel_payload(png):
    payload = []
    offset = 8
    while offset + 12 <= len(png):
        size, kind = struct.unpack_from(">I4s", png, offset)
        if kind == b"IDAT":
            payload.append(png[offset + 8 : offset + 8 + size])
        offset += size + 12
    return zlib.decompress(b"".join(payload))


def corrupt_idat(png):
    """Destroy the stream while keeping the PNG chunk framing and CRC intact."""
    corrupted = bytearray(png)
    offset = 8
    while offset + 12 <= len(png):
        size, kind = struct.unpack_from(">I4s", png, offset)
        if kind == b"IDAT" and size:
            corrupted[offset + 8] = 0
            crc = zlib.crc32(corrupted[offset + 4 : offset + 8 + size])
            struct.pack_into(">I", corrupted, offset + 8 + size, crc)
            return corrupted
        offset += size + 12
    raise AssertionError("Thumbnail contains no nonempty IDAT chunk")


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.kill()
    process.communicate(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("thumbnailer", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("publication_gate", type=Path)
    args = parser.parse_args()
    thumbnailer = args.thumbnailer.resolve()
    fixture = args.fixture.resolve()
    real_bwrap = shutil.which("bwrap")
    if real_bwrap is None:
        parser.error("Bubblewrap is required to exercise cache publication")
    with (
        tempfile.TemporaryDirectory(prefix="majoris-cache-test-") as tmp,
        ExitStack() as children,
    ):
        root = Path(tmp)
        log = root / "starts"
        bwrap = root / "bwrap"
        bwrap.write_text(
            "#!/usr/bin/python3\nimport os,time,sys\n"
            "with open(os.environ['CACHE_TEST_LOG'], 'a') as log: log.write('start\\n')\n"
            "time.sleep(float(os.environ.get('CACHE_TEST_DELAY', '0')))\n"
            f"os.execv({real_bwrap!r}, [{real_bwrap!r}] + sys.argv[1:])\n"
        )
        bwrap.chmod(0o700)
        env = dict(
            os.environ,
            XDG_CACHE_HOME=str(root / "cache"),
            CACHE_TEST_LOG=str(log),
            PATH=f"{root}:{os.environ['PATH']}",
        )
        source = root / "source.png"
        shutil.copyfile(fixture, source)

        def count():
            return len(log.read_text().splitlines()) if log.exists() else 0

        def launch(name, edge=64, delay=0, **overrides):
            process = subprocess.Popen(
                [thumbnailer, source.as_uri(), str(root / (name + ".png")), str(edge)],
                env=dict(env, CACHE_TEST_DELAY=str(delay), **overrides),
                stderr=subprocess.PIPE,
            )
            children.callback(stop_process, process)
            return process

        def wait_for_starts(target):
            end = time.monotonic() + 5
            while count() < target and time.monotonic() < end:
                time.sleep(0.02)
            assert count() == target

        def finish(proc, succeeds=True):
            _, err = proc.communicate(timeout=10)
            assert (proc.returncode == 0) == succeeds, err.decode(errors="replace")

        first = launch("one", delay=0.5)
        wait_for_starts(1)
        waiters = [launch(f"waiter{i}") for i in range(8)]
        finish(first)
        for proc in waiters:
            finish(proc)
        assert count() == 1
        output = (root / "one.png").read_bytes()
        assert output.startswith(b"\x89PNG") and b"Thumb::URI" in output
        assert all((root / f"waiter{i}.png").read_bytes() == output for i in range(8))

        cache = root / "cache" / "majoris-image" / "thumbnails-v2"
        (published,) = cache.glob("*.png")
        published.write_bytes(corrupt_idat(output))
        pending = published.with_suffix(".png.pending")
        pending.write_bytes(b"interrupted publication")
        finish(launch("repaired"))
        assert count() == 2, (
            "The CRC-valid undecodable cache entry was returned instead of repaired"
        )
        assert not pending.exists()
        repaired = (root / "repaired.png").read_bytes()
        assert b"Thumb::URI" in repaired
        # The embedded ICC profile can have a different creation timestamp.
        assert repaired[16:29] == output[16:29] and pixel_payload(
            repaired
        ) == pixel_payload(output)

        locked = launch("locked", 88, delay=1)
        wait_for_starts(3)
        waiting = launch("canceled-waiter", 88)
        time.sleep(0.15)
        waiting.terminate()
        finish(waiting, False)
        finish(locked)
        assert count() == 3 and not (root / "canceled-waiter.png").exists()

        producer = launch("killed", 65, delay=1)
        wait_for_starts(4)
        next_producer = launch("recovered", 65)
        producer.kill()
        finish(producer, False)
        finish(next_producer)
        assert count() == 5 and (root / "recovered.png").read_bytes().startswith(
            b"\x89PNG"
        )

        changing = launch("changed", 66, delay=0.6)
        wait_for_starts(6)
        os.utime(source, ns=(time.time_ns(), time.time_ns() + 50000000))
        finish(changing, False)
        assert not (root / "changed.png").exists()
        finish(launch("new-source", 66))

        left = launch("left", 100, delay=1)
        wait_for_starts(8)
        right = launch("right", 101, delay=1)
        wait_for_starts(9)
        assert left.poll() is None and right.poll() is None
        finish(left)
        finish(right)
        assert all(p.suffix in {".png", ".lock"} for p in cache.iterdir())

        marker = root / "publication-started"
        producer = launch(
            "mid-publication",
            127,
            LD_PRELOAD=str(args.publication_gate.resolve()),
            IMAGEKIT_PUBLICATION_GATE=str(marker),
        )
        deadline = time.monotonic() + 5
        while (
            not marker.exists()
            and producer.poll() is None
            and time.monotonic() < deadline
        ):
            time.sleep(0.02)
        assert marker.is_file() and producer.poll() is None, (
            "Producer never reached pending-file fsync"
        )
        (pending,) = cache.glob("*.png.pending")
        assert pixel_payload(pending.read_bytes()) == pixel_payload(output)
        assert not pending.with_suffix("").exists(), (
            "Pending thumbnail was published before rename"
        )
        producer.kill()
        finish(producer, False)
        finish(launch("recovered-publication", 127))
        assert not pending.exists()
        assert pixel_payload(
            (root / "recovered-publication.png").read_bytes()
        ) == pixel_payload(output)
        assert not (root / "mid-publication.png").exists()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
