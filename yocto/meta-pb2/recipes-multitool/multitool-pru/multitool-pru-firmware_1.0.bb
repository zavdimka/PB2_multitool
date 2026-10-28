SUMMARY = "PocketBeagle 2 multitool PRU firmware"
DESCRIPTION = "PRU0 logic analyzer and PRU1 deterministic JTAG/SWD micro-operation engine"
LICENSE = "MIT & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/../common/files:"

TI_PSSP_BRANCH = "master"
TI_PSSP_SRCREV = "5584926fa3721274e61c5473593cfb7f9f4284d4"
SRC_URI = " \
    git://git.ti.com/git/pru-software-support-package/pru-software-support-package.git;protocol=https;branch=${TI_PSSP_BRANCH} \
    file://multitool_protocol.h \
    file://resource_table.h \
    file://intc_map_0.h \
    file://intc_map_1.h \
    file://pru0_logic.c \
    file://pru1_programmer.c \
"
SRCREV = "${TI_PSSP_SRCREV}"
S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

do_configure[noexec] = "1"

inherit deploy
require recipes-ti/includes/ti-paths.inc

DEPENDS = "ti-cgt-pru-native"
COMPATIBLE_MACHINE = "pocketbeagle2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

PRU_CFLAGS = "-v3 -O2 --endian=little --hardware_mac=on --display_error_number"
PRU_LFLAGS = "--reread_libs --warn_sections --stack_size=0x100 --heap_size=0x100"

do_compile() {
    install -d ${B}/pru0 ${B}/pru1
    for core in 0 1; do
        if [ "$core" = 0 ]; then
            source_file=${WORKDIR}/pru0_logic.c
            intc_file=${WORKDIR}/intc_map_0.h
            linker=${S}/examples/am62x/PRU_RPMsg_Echo_Interrupt0/AM62x_PRU0_intc_rscTbl.cmd
            output=am62x-pru0-fw
        else
            source_file=${WORKDIR}/pru1_programmer.c
            intc_file=${WORKDIR}/intc_map_1.h
            linker=${S}/examples/am62x/PRU_RPMsg_Echo_Interrupt1/AM62x_PRU1_intc_rscTbl.cmd
            output=am62x-pru1-fw
        fi
        install -m 0644 ${WORKDIR}/multitool_protocol.h ${B}/pru${core}/
        install -m 0644 ${WORKDIR}/resource_table.h ${B}/pru${core}/
        install -m 0644 $intc_file ${B}/pru${core}/
        install -m 0644 $source_file ${B}/pru${core}/main.c
        ${TI_CGT_PRU_INSTALL_DIR}/bin/clpru ${PRU_CFLAGS} \
            --include_path=${TI_CGT_PRU_INSTALL_DIR}/include \
            --include_path=${S}/include --include_path=${S}/include/am62x \
            --include_path=${B}/pru${core} -fe ${B}/pru${core}/main.object \
            ${B}/pru${core}/main.c
        ${TI_CGT_PRU_INSTALL_DIR}/bin/clpru ${PRU_CFLAGS} -z \
            -i${TI_CGT_PRU_INSTALL_DIR}/lib -i${TI_CGT_PRU_INSTALL_DIR}/include \
            ${PRU_LFLAGS} -o ${B}/$output ${B}/pru${core}/main.object \
            $linker --library=libc.a --library=${S}/lib/rpmsg_lib.lib
    done
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${B}/am62x-pru0-fw ${D}${nonarch_base_libdir}/firmware/
    install -m 0644 ${B}/am62x-pru1-fw ${D}${nonarch_base_libdir}/firmware/
}

do_deploy() {
    install -m 0644 ${B}/am62x-pru0-fw ${DEPLOYDIR}/
    install -m 0644 ${B}/am62x-pru1-fw ${DEPLOYDIR}/
}
addtask deploy after do_compile before do_build

FILES:${PN} = "${nonarch_base_libdir}/firmware/am62x-pru0-fw ${nonarch_base_libdir}/firmware/am62x-pru1-fw"
INSANE_SKIP:${PN} += "arch already-stripped"
