"""Check the Git index and an extracted POSIX source tarball."""
import argparse
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import tarfile
import tempfile

REQUIRED = ("debian/rules", "tests/installed-consumers.sh")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    options = parser.parse_args()
    if os.name != "posix":
        parser.error("run this check on a POSIX filesystem")

    root = Path(__file__).resolve().parents[1]
    tracked = subprocess.check_output(
        ["git", "-C", root, "ls-files", "--stage", "--", *REQUIRED], text=True
    )
    modes = {}
    for line in tracked.splitlines():
        fields, path = line.split("\t", 1)
        mode, _, stage = fields.split()
        if stage == "0":
            modes[path] = mode

    with tarfile.open(options.archive, "r:*") as archive, tempfile.TemporaryDirectory() as temp:
        for path in REQUIRED:
            if modes.get(path) != "100755":
                raise SystemExit(f"{path}: Git must track mode 100755")
            parts = PurePosixPath(path).parts
            matches = [
                member for member in archive.getmembers()
                if PurePosixPath(member.name).parts[-len(parts):] == parts
            ]
            if len(matches) != 1 or not matches[0].isfile():
                raise SystemExit(f"{path}: expected one archive file")
            entry = matches[0]
            if entry.mode & 0o777 != 0o755:
                raise SystemExit(f"{entry.name}: incorrect archive mode {entry.mode:o}")
            archive.extract(entry, temp, filter="data")
            extracted = Path(temp, entry.name)
            if stat.S_IMODE(extracted.stat().st_mode) != 0o755:
                raise SystemExit(f"{entry.name}: executable mode lost on extraction")
            print(f"{path}: Git 100755, archive and extraction 0755")


if __name__ == "__main__":
    main()
