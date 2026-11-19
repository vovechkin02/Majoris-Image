#!/usr/bin/env python3
"""Build and exercise consumers against the installed framework."""

import argparse
import os
import re
import shlex
import signal
import subprocess
import sys
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

TESTS = Path(__file__).resolve().parents[1]
RELEASE = TESTS / "release"
EXAMPLES = TESTS.parent / "examples"
FIXTURES = TESTS / "fixtures"
CORE_PACKAGE = "majoris-image-1"
GTK_PACKAGE = "majoris-image-gtk4-1"
SYSTEM_PYTHON = "/usr/bin/python3"
TOOL_TIMEOUT_SECONDS = 60
GTK_TIMEOUT_SECONDS = 25
STRESS_PREVIEW_TIMEOUT_SECONDS = 120
STRESS_SELECTION_COUNT = 96
STRESS_THUMBNAIL_ROUNDS = 12


@contextmanager
def start_command(
    args: list[str | Path], env: dict[str, str], *, capture: bool = False
) -> Iterator[subprocess.Popen]:
    """Own the child and its group, including Xvfb and worker descendants."""
    process = subprocess.Popen(
        args,
        env=env,
        start_new_session=True,
        stdout=subprocess.PIPE if capture else None,
        text=True,
    )
    try:
        yield process
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=TOOL_TIMEOUT_SECONDS)
        if process.stdout is not None:
            process.stdout.close()


def run(
    env: dict[str, str],
    *args: str | Path,
    timeout: int = TOOL_TIMEOUT_SECONDS,
    capture: bool = False,
) -> str:
    """Runs an installed-consumer command and raises on nonzero exit status."""
    with start_command(list(args), env, capture=capture) as process:
        output, _ = process.communicate(timeout=timeout)
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, args, output=output)
    return output or ""


def check_headless(env: dict[str, str]) -> None:
    """Builds and exercises a Core-only consumer without pulling in GTK dependencies."""
    for package in (CORE_PACKAGE, GTK_PACKAGE):
        run(env, "pkg-config", "--atleast-version=2.0.0", package)
    core_libdir = Path(
        run(env, "pkg-config", "--variable=libdir", CORE_PACKAGE, capture=True).strip()
    )
    gtk_libdir = Path(
        run(env, "pkg-config", "--variable=libdir", GTK_PACKAGE, capture=True).strip()
    )
    run(
        env,
        sys.executable,
        RELEASE / "check_abi.py",
        core_libdir / "libmajoris-image.so.2",
        gtk_libdir / "libmajoris-image-gtk4.so.2",
    )
    run(
        env,
        "valac",
        "--pkg",
        CORE_PACKAGE,
        "--pkg",
        "posix",
        TESTS / "benchmarks/benchmark.vala",
        "-o",
        "headless",
    )
    run(env, "./headless", "open", FIXTURES / "rgb16.png")
    linked = run(env, "ldd", "./headless", capture=True)
    leaked = [
        line for line in linked.splitlines() if re.search(r"libgtk|libgdk-", line)
    ]
    if leaked:
        raise RuntimeError(
            "GTK leaked into the headless dependency graph:\n" + "\n".join(leaked)
        )


def check_gtk(env: dict[str, str], large: Path) -> None:
    """Builds and exercises installed GTK and Vala image-view consumers."""
    png = FIXTURES / "rgb16.png"
    exr = FIXTURES / "multipart.exr"
    flags = run(env, "pkg-config", "--cflags", "--libs", GTK_PACKAGE, capture=True)
    run(env, "cc", EXAMPLES / "builder.c", *shlex.split(flags), "-o", "builder")
    run(env, "xvfb-run", "-a", "./builder", png, timeout=GTK_TIMEOUT_SECONDS)
    run(
        env,
        "valac",
        "--pkg",
        GTK_PACKAGE,
        RELEASE / "test-installed-viewer.vala",
        "-o",
        "view-check",
    )
    run(
        env,
        "xvfb-run",
        "-a",
        "./view-check",
        png,
        FIXTURES / "corrupt.bin",
        timeout=GTK_TIMEOUT_SECONDS,
    )
    run(
        env,
        "valac",
        "--pkg",
        GTK_PACKAGE,
        EXAMPLES / "files-preview.vala",
        "-o",
        "files",
    )
    run(
        env,
        "valac",
        "--pkg",
        GTK_PACKAGE,
        EXAMPLES / "photos-viewer.vala",
        "-o",
        "photos",
    )
    run(env, "xvfb-run", "-a", "./files", png, exr, timeout=60)
    export = Path("photo.png")
    run(env, "xvfb-run", "-a", "./photos", large, exr, export, timeout=90)
    if export.stat().st_size == 0:
        raise RuntimeError(f"Photos produced an empty export: {export}")


def check_stress(env: dict[str, str]) -> None:
    # Independent processes deliberately share the worker staging directory.
    """Exercises repeated viewer lifecycle and thumbnail operations using installed
    packages.
    """
    work = Path.cwd() / "work"
    work.mkdir()
    png = FIXTURES / "rgb16.png"
    exr = FIXTURES / "multipart.exr"
    shared_env = dict(env, TMPDIR=str(work))
    preview_env = dict(
        shared_env, MAJORIS_FILES_SWITCH_COUNT=str(STRESS_SELECTION_COUNT)
    )
    preview_args = [
        "timeout",
        "--foreground",
        str(STRESS_PREVIEW_TIMEOUT_SECONDS),
        "xvfb-run",
        "-a",
        "./files",
        png,
        exr,
    ]
    # Keep timeout in the preview's group so it can enforce its own deadline
    # while the foreground waits for independently bounded thumbnail requests.
    with start_command(preview_args, preview_env) as preview:
        for _ in range(STRESS_THUMBNAIL_ROUNDS):
            for source in (png, exr):
                run(
                    shared_env,
                    "./headless",
                    "thumbnail",
                    source,
                    timeout=GTK_TIMEOUT_SECONDS,
                )
        if status := preview.wait(timeout=STRESS_PREVIEW_TIMEOUT_SECONDS):
            raise subprocess.CalledProcessError(status, preview_args)
    if any(work.iterdir()):
        raise RuntimeError(f"Concurrent consumers left worker artifacts in {work}")
    print(
        f"Concurrent installed stress passed with {STRESS_SELECTION_COUNT} selections "
        f"and {2 * STRESS_THUMBNAIL_ROUNDS} thumbnail requests"
    )


def check_introspection_and_thumbnail(env: dict[str, str]) -> None:
    """Verifies GIR/GtkBuilder access and installed thumbnail integration."""
    checks = RELEASE / "installed_gi.py"
    run(
        env,
        "xvfb-run",
        "-a",
        SYSTEM_PYTHON,
        checks,
        "builder",
        timeout=GTK_TIMEOUT_SECONDS,
    )
    run(
        env,
        "/usr/libexec/majoris-image-thumbnailer",
        (FIXTURES / "rgb16.png").as_uri(),
        "thumbnail.png",
        "128",
    )
    run(env, SYSTEM_PYTHON, checks, "thumbnail")
    descriptor = Path("/usr/share/thumbnailers/majoris-image.thumbnailer")
    if not descriptor.is_file():
        raise RuntimeError(f"Missing installed thumbnailer descriptor: {descriptor}")


def check_gainmap(env: dict[str, str]) -> None:
    """Checks installed gain-map codec behavior when the optional feature is enabled."""
    run(
        env,
        "valac",
        "--pkg",
        CORE_PACKAGE,
        RELEASE / "test-installed-gainmap.vala",
        "-o",
        "gainmap",
    )
    run(env, "./gainmap", FIXTURES / "avif-hdr-sequence.avif", "output.jpg")


def interrupted(signum: int, _frame: object) -> None:
    """Handles a termination signal without leaving child workers running."""
    raise SystemExit(128 + signum)


def main(argv: list[str] | None = None) -> int:
    """Selects and runs installed API and package integration checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument(
        "large_tiff", type=Path, nargs="?", help="Generated Photos fixture"
    )
    inputs.add_argument(
        "--gainmap", action="store_true", help="Check the installed gain-map profile"
    )
    args = parser.parse_args(argv)
    if args.large_tiff is not None:
        args.large_tiff = args.large_tiff.resolve()
        if not args.large_tiff.is_file() or not os.access(args.large_tiff, os.R_OK):
            parser.error(f"Photos fixture is unreadable: {args.large_tiff}")

    if not os.environ.get("IMAGEKIT_TEST_ROOT"):
        arguments = ["--gainmap"] if args.gainmap else [str(args.large_tiff)]
        os.execv(
            sys.executable,
            [
                sys.executable,
                str(TESTS / "support/run_isolated.py"),
                sys.executable,
                str(Path(__file__).resolve()),
                *arguments,
            ],
        )

    os.chdir(os.environ["IMAGEKIT_TEST_ROOT"])
    env = os.environ.copy()
    env.pop("MAJORIS_IMAGE_WORKER", None)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        if args.gainmap:
            check_gainmap(env)
        else:
            check_headless(env)
            check_gtk(env, args.large_tiff)
            if env.get("MAJORIS_TEST_STRESS") == "1":
                check_stress(env)
            check_introspection_and_thumbnail(env)
            print(
                "Installed headless, Vala GTK, C GtkBuilder, introspection, "
                "Photos export and thumbnail consumers passed"
            )
    except subprocess.CalledProcessError as error:
        if error.output:
            print(error.output, file=sys.stderr, end="")
        print(error, file=sys.stderr)
        return error.returncode if error.returncode > 0 else 128 - error.returncode
    except subprocess.TimeoutExpired as error:
        print(error, file=sys.stderr)
        return 124
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
