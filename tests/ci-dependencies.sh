#!/bin/sh
# Provision the disposable Debian 13 CI container.
set -eu
apt-get update
apt-get install -y --no-install-recommends git ca-certificates build-essential binutils \
    debhelper meson ninja-build valac pkgconf python3 python3-gi \
    libglib2.0-dev libgee-0.8-dev libjson-glib-dev zlib1g-dev libgtk-4-dev gir1.2-gtk-4.0 \
    libglycin-1-dev glycin-loaders libvips-dev libraw-dev liblcms2-dev libheif-dev \
    libjxl-dev libopenexr-dev librsvg2-dev libgexiv2-dev libxtst-dev libavif-dev \
    libheif-plugin-libde265 libheif-plugin-dav1d girepository-tools bubblewrap xvfb xauth
