#!/bin/sh
# Run against an installed framework from outside its build tree.
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ne 1 ] || [ ! -r "$1" ]; then
    echo "Usage: $0 /path/to/generated/large.tiff (Photos fixture is required)" >&2
    exit 2
fi
large=$(realpath "$1")
if [ -z "${IMAGEKIT_TEST_ROOT:-}" ]; then
    exec python3 "$source_root/tests/run-isolated.py" sh "$0" "$large"
fi
cd "$IMAGEKIT_TEST_ROOT"
scratch=$IMAGEKIT_TEST_ROOT
unset MAJORIS_IMAGE_WORKER

pkg-config --atleast-version=2.0.0 majoris-image-1
pkg-config --atleast-version=2.0.0 majoris-image-gtk4-1
core_libdir=$(pkg-config --variable=libdir majoris-image-1)
gtk_libdir=$(pkg-config --variable=libdir majoris-image-gtk4-1)
python3 "$source_root/tests/check-abi.py" "$core_libdir/libmajoris-image.so.2" "$gtk_libdir/libmajoris-image-gtk4.so.2"
valac --pkg majoris-image-1 "$source_root/tests/benchmark.vala" -o headless
./headless open "$source_root/tests/fixtures/rgb16.png"
if ldd ./headless | grep -E 'libgtk|libgdk-'; then
    echo "GTK leaked into the headless dependency graph" >&2
    exit 1
fi
cc "$source_root/examples/builder.c" $(pkg-config --cflags --libs majoris-image-gtk4-1) -o builder
timeout 25 xvfb-run -a ./builder "$source_root/tests/fixtures/rgb16.png"
valac --pkg majoris-image-gtk4-1 "$source_root/tests/installed-viewer.vala" -o view-check
timeout 25 xvfb-run -a ./view-check "$source_root/tests/fixtures/rgb16.png" "$source_root/tests/fixtures/corrupt.bin"
valac --pkg majoris-image-gtk4-1 "$source_root/examples/files-preview.vala" -o files
valac --pkg majoris-image-gtk4-1 "$source_root/examples/photos-viewer.vala" -o photos
timeout 60 xvfb-run -a ./files "$source_root/tests/fixtures/rgb16.png" "$source_root/tests/fixtures/multipart.exr"
timeout 90 xvfb-run -a ./photos "$large" "$source_root/tests/fixtures/multipart.exr" photo.png
test -s photo.png
if [ "${MAJORIS_TEST_STRESS:-0}" = 1 ]; then
    mkdir "$scratch/work"
    png="$source_root/tests/fixtures/rgb16.png"
    exr="$source_root/tests/fixtures/multipart.exr"
    TMPDIR="$scratch/work" MAJORIS_FILES_SWITCH_COUNT=96 timeout 120 xvfb-run -a ./files "$png" "$exr" &
    preview_pid=$!
    # Independent processes use the same cache namespace and alternate sources.
    (
        export TMPDIR="$scratch/work"
        for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
            timeout 25 ./headless thumbnail "$png"
            timeout 25 ./headless thumbnail "$exr"
        done
    ) &
    thumbnail_pid=$!
    preview_status=0
    thumbnail_status=0
    wait "$preview_pid" || preview_status=$?
    wait "$thumbnail_pid" || thumbnail_status=$?
    test "$preview_status" -eq 0
    test "$thumbnail_status" -eq 0
    test -z "$(find "$scratch/work" -mindepth 1 -print -quit)"
    echo "Concurrent installed stress passed with 96 selections and 24 thumbnail requests"
fi
timeout 25 xvfb-run -a /usr/bin/python3 - <<'PY'
import gi
gi.require_version("Gtk", "4.0")
gi.require_version("ImageKit", "2.0")
gi.require_version("ImageKitGtk", "2.0")
from gi.repository import Gtk, ImageKit, ImageKitGtk
Gtk.init()
assert ImageKit.DecoderRegistry()
ImageKitGtk.ImageView.register_type()
builder = Gtk.Builder.new_from_string(
    "<interface><object class='ImageKitView' id='view'>"
    "<property name='scroll-zoom-mode'>control</property>"
    "<property name='nearest-neighbor'>true</property>"
    "<property name='accessible-description'>Selected image</property>"
    "</object></interface>", -1
)
view = builder.get_object("view")
assert isinstance(view, ImageKitGtk.ImageView)
assert view.props.scroll_zoom_mode == ImageKitGtk.ScrollZoomMode.CONTROL
assert view.props.nearest_neighbor
assert view.props.accessible_description == "Selected image"
assert view.get_accessible_role() == Gtk.AccessibleRole.IMG
assert ImageKitGtk.PresentationStatus.EXTERNAL_UNVERIFIED
PY

/usr/libexec/majoris-image-thumbnailer "file://$source_root/tests/fixtures/rgb16.png" thumbnail.png 128
/usr/bin/python3 - <<'PY'
import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf

thumbnail = GdkPixbuf.Pixbuf.new_from_file("thumbnail.png")
assert (thumbnail.get_width(), thumbnail.get_height()) == (24, 16)
assert thumbnail.get_option("tEXt::Thumb::URI").endswith("/rgb16.png")
assert thumbnail.get_option("tEXt::Thumb::MTime").isdecimal()
pixels = thumbnail.get_pixels()
offset = (thumbnail.get_height() - 1) * thumbnail.get_rowstride()
assert len(pixels) >= offset + thumbnail.get_width() * thumbnail.get_n_channels()
assert pixels[:3] != pixels[offset:offset + 3], "The gradient thumbnail became a flat image"
PY
test -f /usr/share/thumbnailers/majoris-image.thumbnailer
echo "Installed headless, Vala GTK, C GtkBuilder, introspection, Photos export and thumbnail consumers passed"
