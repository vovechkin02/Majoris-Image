"""Exercise cache and worker behavior across independent thumbnailer processes."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

thumbnailer, fixture = (str(Path(p).resolve()) for p in sys.argv[1:])
real_bwrap = shutil.which("bwrap")
if real_bwrap is None:
    print("Bubblewrap unavailable")
    sys.exit(77)

def pixel_payload(png):
    payload = []
    offset = 8
    while offset + 12 <= len(png):
        size, kind = struct.unpack_from(">I4s", png, offset)
        if kind == b"IDAT":
            payload.append(png[offset + 8 : offset + 8 + size])
        offset += size + 12
    return zlib.decompress(b"".join(payload))


with tempfile.TemporaryDirectory(prefix="majoris-cache-test-") as tmp:
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
    env = dict(os.environ, XDG_CACHE_HOME=str(root / "cache"),
               CACHE_TEST_LOG=str(log), PATH=f"{root}:{os.environ['PATH']}")
    source = root / "source.png"
    shutil.copyfile(fixture, source)

    def count():
        return len(log.read_text().splitlines()) if log.exists() else 0

    def launch(name, edge=64, delay=0):
        return subprocess.Popen(
            [thumbnailer, source.as_uri(), str(root / (name + ".png")), str(edge)],
            env=dict(env, CACHE_TEST_DELAY=str(delay)), stderr=subprocess.PIPE,
        )

    def wait_for_starts(target):
        end = time.monotonic() + 5
        while count() < target and time.monotonic() < end:
            time.sleep(.02)
        assert count() == target

    def finish(proc, succeeds=True):
        _, err = proc.communicate(timeout=10)
        assert (proc.returncode == 0) == succeeds, err.decode(errors="replace")

    first = launch("one", delay=.5)
    wait_for_starts(1)
    waiters = [launch(f"waiter{i}") for i in range(4)]
    finish(first)
    for proc in waiters:
        finish(proc)
    assert count() == 1
    output = (root / "one.png").read_bytes()
    assert output.startswith(b"\x89PNG") and b"Thumb::URI" in output
    assert all((root / f"waiter{i}.png").read_bytes() == output for i in range(4))

    cache = root / "cache" / "majoris-image" / "thumbnails-v2"
    published, = cache.glob("*.png")
    published.write_bytes(b"truncated")
    finish(launch("repaired"))
    assert count() == 2
    repaired = (root / "repaired.png").read_bytes()
    assert b"Thumb::URI" in repaired
    # The embedded ICC profile can have a different creation timestamp.
    assert repaired[16:29] == output[16:29] and pixel_payload(repaired) == pixel_payload(output)

    locked = launch("locked", 88, delay=1)
    wait_for_starts(3)
    waiting = launch("cancelled-waiter", 88)
    time.sleep(.15)
    waiting.terminate()
    finish(waiting, False)
    finish(locked)
    assert count() == 3 and not (root / "cancelled-waiter.png").exists()

    producer = launch("killed", 65, delay=1)
    wait_for_starts(4)
    next_producer = launch("recovered", 65)
    producer.kill()
    finish(producer, False)
    finish(next_producer)
    assert count() == 5 and (root / "recovered.png").read_bytes().startswith(b"\x89PNG")

    changing = launch("changed", 66, delay=.6)
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
