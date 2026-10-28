BBCLASSEXTEND += "native"

DEPENDS:append:class-native = " fontconfig-native libxkbcommon-native wayland-native"

SLINT_RENDERERS:class-native = "software"
SLINT_BACKENDS:class-native = "winit-wayland"
SLINT_CARGO_FEATURES:class-native = ""
CARGO_FEATURES:class-native = "backend-winit-wayland renderer-software"
