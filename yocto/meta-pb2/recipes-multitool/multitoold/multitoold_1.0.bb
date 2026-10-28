SUMMARY = "Hardware orchestration service for PB2 multitool"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/../common/files:"
CPPFLAGS:append = " -I${WORKDIR}"

SRC_URI = " \
    file://multitoold.c \
    file://scope_ring.c \
    file://scope_ring.h \
    file://iio_scope.c \
    file://iio_scope.h \
    file://remoteproc.c \
    file://remoteproc.h \
    file://rpmsg_link.c \
    file://rpmsg_link.h \
    file://openocd_bridge.c \
    file://openocd_bridge.h \
    file://openocd_remote.c \
    file://multitool_protocol.h \
    file://jtag-pins.conf \
    file://pb2-multitool.cfg \
    file://openocd-remote.conf \
    file://udhcpd-usb.conf \
    file://Makefile \
    file://multitoold.init \
    file://multitool-usb.init \
    file://multitool-openocd-remote.init \
"

S = "${WORKDIR}"
inherit update-rc.d

INITSCRIPT_NAME = "multitoold"
INITSCRIPT_PARAMS = "defaults 20"

do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"
}

do_install() {
    install -d ${D}${sbindir} ${D}${sysconfdir}/init.d \
        ${D}${sysconfdir}/multitool \
        ${D}${datadir}/openocd/scripts/interface
    install -m 0755 ${S}/multitoold ${D}${sbindir}/multitoold
    install -m 0755 ${S}/multitool-openocd-remote ${D}${sbindir}/multitool-openocd-remote
    install -m 0755 ${WORKDIR}/multitoold.init ${D}${sysconfdir}/init.d/multitoold
    install -m 0755 ${WORKDIR}/multitool-usb.init ${D}${sysconfdir}/init.d/multitool-usb
    install -m 0755 ${WORKDIR}/multitool-openocd-remote.init \
        ${D}${sysconfdir}/init.d/multitool-openocd-remote
    install -m 0644 ${WORKDIR}/jtag-pins.conf ${D}${sysconfdir}/multitool/jtag-pins.conf
    install -m 0644 ${WORKDIR}/openocd-remote.conf \
        ${D}${sysconfdir}/multitool/openocd-remote.conf
    install -m 0644 ${WORKDIR}/udhcpd-usb.conf \
        ${D}${sysconfdir}/multitool/udhcpd-usb.conf
    install -m 0644 ${WORKDIR}/pb2-multitool.cfg \
        ${D}${datadir}/openocd/scripts/interface/pb2-multitool.cfg
}

CONFFILES:${PN} += " \
    ${sysconfdir}/multitool/jtag-pins.conf \
    ${sysconfdir}/multitool/openocd-remote.conf \
"
FILES:${PN} += "${datadir}/openocd/scripts/interface/pb2-multitool.cfg"
