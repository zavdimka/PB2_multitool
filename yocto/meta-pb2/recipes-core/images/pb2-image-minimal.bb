SUMMARY = "Fast-booting minimal image for PocketBeagle 2"
DESCRIPTION = "Small BusyBox/mdev system with PB2 multitool services and a direct-KMS Slint UI."
LICENSE = "MIT"

inherit core-image

IMAGE_INSTALL = "base-files base-passwd busybox busybox-mdev busybox-syslog busybox-udhcpd eudev grub-efi kernel kernel-devicetree kernel-image-image kernel-module-iio-trig-hrtimer multitool-pru-firmware multitoold multitool-ui netbase openocd seatd ti-pka-fw update-alternatives-opkg"
IMAGE_LINGUAS = ""
IMAGE_FEATURES = ""

# Slint software renderer, libinput/libseat and OpenOCD still fit comfortably.
IMAGE_ROOTFS_SIZE = "131072"
IMAGE_ROOTFS_EXTRA_SPACE = "0"

WKS_FILE = "pb2-sdimage.wks.in"
IMAGE_FSTYPES = "wic.xz wic.bmap tar.xz"

# Reduce console output while preserving the BSP-selected serial getty.
TI_WKS_BOOTLOADER_APPEND = "quiet loglevel=3"

# BusyBox rcS does not execute update-rc.d package postinsts in this image.
# Make the boot order explicit: USB network, hardware/OpenOCD, then UI.
pb2_enable_init_services() {
    install -d ${IMAGE_ROOTFS}${sysconfdir}/rcS.d \
        ${IMAGE_ROOTFS}${sysconfdir}/rc5.d \
        ${IMAGE_ROOTFS}${sysconfdir}/rc6.d
    ln -snf ../init.d/udev ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S04udev
    ln -snf ../init.d/multitool-usb ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S08multitool-usb
    ln -snf ../init.d/seatd ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S09seatd
    ln -snf ../init.d/multitoold ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S20multitoold
    ln -snf ../init.d/multitool-openocd-remote ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S24multitool-openocd-remote
    ln -snf ../init.d/multitool-ui ${IMAGE_ROOTFS}${sysconfdir}/rcS.d/S30multitool-ui
    ln -snf ../init.d/multitool-ui ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K10multitool-ui
    ln -snf ../init.d/multitool-openocd-remote ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K15multitool-openocd-remote
    ln -snf ../init.d/multitoold ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K20multitoold
    ln -snf ../init.d/seatd ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K30seatd
    ln -snf ../init.d/multitool-usb ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K35multitool-usb
    ln -snf ../init.d/udev ${IMAGE_ROOTFS}${sysconfdir}/rc6.d/K40udev
}
ROOTFS_POSTPROCESS_COMMAND:append = " pb2_enable_init_services;"
