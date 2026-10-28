FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:pocketbeagle2 = " file://pb2-fastboot.config"
UBOOT_CONFIG_FRAGMENTS:append:pocketbeagle2 = " pb2-fastboot.config"

do_configure:prepend:pocketbeagle2() {
    install -m 0644 ${WORKDIR}/pb2-fastboot.config ${S}/configs/pb2-fastboot.config
}
