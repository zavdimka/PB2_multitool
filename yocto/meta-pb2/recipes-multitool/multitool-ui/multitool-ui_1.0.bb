SUMMARY = "Touch-oriented Slint UI for the PB2 multitool"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://CMakeLists.txt \
    file://main.cpp \
    file://appwindow.slint \
    file://multitool-ui-launch \
    file://multitool-ui.init \
"

S = "${WORKDIR}"

inherit cmake slint update-rc.d

RDEPENDS:${PN} += "multitoold"

INITSCRIPT_NAME = "multitool-ui"
INITSCRIPT_PARAMS = "defaults 30"

do_install:append() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/multitool-ui-launch ${D}${bindir}/multitool-ui-launch
    install -m 0755 ${WORKDIR}/multitool-ui.init ${D}${sysconfdir}/init.d/multitool-ui
}