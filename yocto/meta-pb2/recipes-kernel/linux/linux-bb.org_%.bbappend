FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:pocketbeagle2 = " \
    file://0001-arm64-dts-ti-add-BB2-multitool-pinmux.patch \
    file://0002-hwmon-ina238-add-runtime-shunt-gain.patch \
    file://0003-regulator-add-tps55289-support.patch \
    file://0004-arm64-dts-ti-add-multitool-power-devices.patch \
    file://0005-arm64-dts-ti-add-lcdwiki-msp4031.patch \
    file://0006-input-edt-ft5x06-add-ft6336-compatible.patch \
    file://lcdwiki,msp4031.bin \
    file://lcdwiki-msp4031-init.txt \
    file://pb2-multitool.cfg \
"

KERNEL_CONFIG_FRAGMENTS:append:pocketbeagle2 = " ${WORKDIR}/pb2-multitool.cfg"

do_configure:prepend:pocketbeagle2() {
    install -d ${S}/firmware
    install -m 0644 ${WORKDIR}/lcdwiki,msp4031.bin ${S}/firmware/
}