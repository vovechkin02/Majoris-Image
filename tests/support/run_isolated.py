"""Run a test with a private XDG environment and an owned process group."""

import argparse
import os
import shutil
import signal
import subprocess
import tempfile
from pathlib import Path


def run(arguments: list[str]) -> int:
    root = Path(tempfile.mkdtemp(prefix="imagekit-test-"))
    env = dict(os.environ, IMAGEKIT_TEST_ROOT=str(root))
    for name in (
        "HOME",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "XDG_RUNTIME_DIR",
        "TMPDIR",
    ):
        directory = root / name.lower()
        directory.mkdir(mode=0o700)
        env[name] = str(directory)
    # GTK still needs the caller's Wayland socket and X11 cookie, if any.
    for name in ("WAYLAND_DISPLAY", "XAUTHORITY"):
        if name in os.environ or name == "XAUTHORITY":
            path = Path(os.environ.get(name, Path.home() / ".Xauthority"))
            if name == "WAYLAND_DISPLAY" and not path.is_absolute():
                path = Path(os.environ.get("XDG_RUNTIME_DIR", "/run")) / path
            if path.exists():
                env[name] = str(path.resolve())
    command = [part.replace("@TEST_ROOT@", str(root)) for part in arguments]
    process = None

    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        process = subprocess.Popen(command, env=env, start_new_session=True)
        status = process.wait()
        return status if status >= 0 else 128 - status
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                if process.poll() is None:
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        pass
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        if os.environ.get("IMAGEKIT_KEEP_TEST_ARTIFACTS") == "1":
            print(f"Retained test artifacts at {root}")
        else:
            shutil.rmtree(root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error("a test command is required")
    if args.repeat < 1:
        parser.error("--repeat must be positive")
    for _ in range(args.repeat):
        status = run(args.command)
        if status:
            return status
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
