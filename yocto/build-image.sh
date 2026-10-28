#!/bin/bash
set -e
cd /home/dimka/PB2_multitool
. yocto/sources/poky/oe-init-build-env yocto/build >/dev/null
exec bitbake pb2-image-minimal
