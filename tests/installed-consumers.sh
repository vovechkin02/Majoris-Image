#!/bin/sh
# Run against an installed framework from outside its build tree.
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -f "$scratch/builder" "$scratch/headless" "$scratch/demo" "$scratch/files" "$scratch/photos" "$scratch/photo.png" "$scratch/demo-resources.c" "$scratch/thumbnail.png"; rmdir "$scratch"' EXIT
cd "$scratch"
unset MAJORIS_IMAGE_WORKER

pkg-config --atleast-version=1.1.1 majoris-image-1
pkg-config --atleast-version=1.1.1 majoris-image-gtk4-1
valac --pkg majoris-image-1 "$source_root/tests/benchmark.vala" -o headless
./headless open "$source_root/tests/fixtures/rgb16.png"
if ldd ./headless | grep -E 'libgtk|libgdk-'; then
    echo "GTK leaked into the headless dependency graph" >&2
    exit 1
fi
cc "$source_root/examples/builder.c" $(pkg-config --cflags --libs majoris-image-gtk4-1) -o builder
xvfb-run -a ./builder "$source_root/tests/fixtures/rgb16.png"
glib-compile-resources --sourcedir="$source_root/examples" --generate-source \
    --target=demo-resources.c "$source_root/examples/demo.gresource.xml"
valac --pkg majoris-image-gtk4-1 "$source_root/examples/demo.vala" demo-resources.c -o demo
xvfb-run -a sh -c 'status=0; timeout 3 ./demo || status=$?; test "$status" -eq 124'
valac --pkg majoris-image-gtk4-1 "$source_root/examples/files-preview.vala" -o files
valac --pkg majoris-image-gtk4-1 "$source_root/examples/photos-viewer.vala" -o photos
xvfb-run -a ./files "$source_root/tests/fixtures/rgb16.png" "$source_root/tests/fixtures/multipart.exr"
if [ -f "$source_root/tests/fixtures/large.tiff" ]; then
    xvfb-run -a ./photos "$source_root/tests/fixtures/large.tiff" "$source_root/tests/fixtures/multipart.exr" photo.png
fi
xvfb-run -a /usr/bin/python3 - <<'PY'
import gi
gi.require_version("Gtk", "4.0")
gi.require_version("MajorisImage", "1.0")
gi.require_version("MajorisImageGtk", "1.0")
from gi.repository import Gtk, MajorisImage, MajorisImageGtk
Gtk.init()
assert MajorisImage.DecoderRegistry()
MajorisImageGtk.ImageView.register_type()
builder = Gtk.Builder.new_from_string(
    "<interface><object class='MajorisImageView' id='view'/></interface>", -1
)
assert isinstance(builder.get_object("view"), MajorisImageGtk.ImageView)
PY

/usr/libexec/majoris-image-thumbnailer "file://$source_root/tests/fixtures/rgb16.png" thumbnail.png 128
/usr/bin/python3 - <<'PY'
from pathlib import Path
png = Path("thumbnail.png").read_bytes()
assert png.startswith(b"\x89PNG\r\n\x1a\n")
assert b"Thumb::URI" in png and b"Thumb::MTime" in png
PY
test -f /usr/share/thumbnailers/majoris-image.thumbnailer
echo "Installed headless, Vala GTK, C GtkBuilder, introspection and thumbnail consumers passed"
