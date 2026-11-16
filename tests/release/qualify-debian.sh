#!/bin/sh
# Refuse to qualify vendor packages from an unprovisioned runner.
set -eu
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 RELEASE_BUILD" >&2
    exit 2
fi
build=$(realpath "$1")
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
[ "$(id -u)" -ne 0 ] || { echo "Run worker and package builds as an unprivileged user" >&2; exit 2; }
work=$(mktemp -d)
trap 'find "$work" -depth -delete' EXIT
mkdir "$work/source"
git -c tar.umask=0022 archive --format=tar.gz HEAD > "$work/source.tar.gz"
python3 tests/release/check_release_modes.py "$work/source.tar.gz"
tar -xzf "$work/source.tar.gz" -C "$work/source"
(
    cd "$work/source"
    dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b
)
set -- "$work"/libmajoris-image2_*.deb "$work"/libmajoris-image-gtk4-2_*.deb \
    "$work"/libmajoris-image-dev_*.deb "$work"/majoris-image-worker_*.deb \
    "$work"/majoris-image-thumbnailer_*.deb
sudo apt-get remove -y libmajoris-image-dev libmajoris-image-gtk4-2 \
    majoris-image-thumbnailer libmajoris-image2 majoris-image-worker
sudo apt-get install -y "$@"
meson compile -C "$build"
python3 tests/release/installed_consumers.py "$build/tests/large.tiff"
sudo apt-get remove -y libmajoris-image-dev libmajoris-image-gtk4-2 \
    majoris-image-thumbnailer libmajoris-image2 majoris-image-worker
test ! -e /usr/libexec/majoris-image-worker
sudo apt-get install -y "$@"
python3 tests/release/installed_consumers.py --gainmap
