"""Verify the installed GI bindings and exported thumbnail."""

import argparse

import gi


def check_builder() -> None:
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
        "</object></interface>",
        -1,
    )
    view = builder.get_object("view")
    assert isinstance(view, ImageKitGtk.ImageView)
    assert view.props.scroll_zoom_mode == ImageKitGtk.ScrollZoomMode.CONTROL
    assert view.props.nearest_neighbor
    assert view.props.accessible_description == "Selected image"
    assert view.get_accessible_role() == Gtk.AccessibleRole.IMG
    assert ImageKitGtk.PresentationStatus.EXTERNAL_UNVERIFIED


def check_thumbnail() -> None:
    gi.require_version("GdkPixbuf", "2.0")
    from gi.repository import GdkPixbuf

    thumbnail = GdkPixbuf.Pixbuf.new_from_file("thumbnail.png")
    assert (thumbnail.get_width(), thumbnail.get_height()) == (24, 16)
    assert thumbnail.get_option("tEXt::Thumb::URI").endswith("/rgb16.png")
    assert thumbnail.get_option("tEXt::Thumb::MTime").isdecimal()
    pixels = thumbnail.get_pixels()
    offset = (thumbnail.get_height() - 1) * thumbnail.get_rowstride()
    assert len(pixels) >= offset + thumbnail.get_width() * thumbnail.get_n_channels()
    assert pixels[:3] != pixels[offset : offset + 3], (
        "The gradient thumbnail became a flat image"
    )


def main() -> int:
    checks = {"builder": check_builder, "thumbnail": check_thumbnail}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("check", choices=checks)
    checks[parser.parse_args().check]()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
