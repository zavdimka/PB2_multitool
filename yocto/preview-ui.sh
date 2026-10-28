#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VIEWER="$SCRIPT_DIR/build/tmp/sysroots-components/x86_64/slint-viewer-native/usr/bin/slint-viewer"
UI="$SCRIPT_DIR/meta-pb2/recipes-multitool/multitool-ui/files/appwindow.slint"

if [ ! -x "$VIEWER" ]; then
    echo "slint-viewer-native is not built." >&2
    echo "Run: source yocto/sources/poky/oe-init-build-env yocto/build && bitbake slint-viewer-native" >&2
    exit 1
fi

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "No WSLg display found. Start this script from a WSLg-enabled terminal." >&2
    exit 1
fi

export SLINT_BACKEND="${SLINT_BACKEND:-winit-software}"
exec "$VIEWER" --auto-reload "$UI" "$@"
