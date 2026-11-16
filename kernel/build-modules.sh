#!/bin/bash
# build-modules.sh - Build every out-of-tree driver in kernel/drivers/ against OUR image's tree
# Run this script in WSL (Linux), after build-image.sh: the modules link against the
# Module.symvers and vermagic of ~/rw-kbuild-73, so a .ko built here loads on a unit that
# boots that image and on no other kernel. The vendor-kernel modules (xpad and friends) are
# built by usb_host/build-and-deploy.sh instead (kernel/build-bt-modules.sh --set usb), from the same image tree.
#
# Produces: <out>/<driver>.ko, one per kernel/drivers/<driver>/, and with --deploy copies them to
# /lib/modules/<release>/extra/ on the unit with --deploy; touch-module loads only the touch driver there at boot.
# The BT, USB-audio, AX88179 and xpad modules are not built here: they come from build-image.sh's modules_install.

# shellcheck disable=SC2029,SC1091  # the device paths are ours and meant to expand here; arca-ssh.sh is sourced by path
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_WORK="${HOME}/rw-kbuild-73"
CROSS_COMPILE="arm-linux-gnueabihf-"
ARCH="arm"
OUT_DIR=""
DEVICE_IP=""
DEVICE_MODDIR=""

usage() {
    cat <<'EOF'
Usage: build-modules.sh --out <dir> [--image <work>] [--deploy <ip>]

Copies each kernel/drivers/<driver>/ into ~/rw-kbuild-73/modules (never builds on /mnt/c)
and builds it with M= against ~/rw-kbuild-73/linux-7.3.4, which build-image.sh must
already have built. Copies each resulting .ko to <dir>.

--image <work> the image's build tree parent: the --work given to build-image.sh, whose
               modules must match the image the unit boots (default: ~/rw-kbuild-73).

--deploy <ip>  also copy each .ko to /lib/modules/<uname -r of the unit>/extra/ on root@<ip> and
               check its md5 there. A running touch module is not reloaded (that fails on
               7.3.4): reboot the unit. The loader and its boot link come from provision.sh.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --out)     shift; OUT_DIR="${1:?--out needs a directory}" ;;
        --deploy)  shift; DEVICE_IP="${1:?--deploy needs an IP}" ;;
        --image)   shift; IMAGE_WORK="${1:?--image needs a directory}" ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "ERROR: unknown argument $1"; usage; exit 1 ;;
    esac
    shift
done
[ -n "$OUT_DIR" ] || { usage; exit 1; }
KERNEL_DIR="${IMAGE_WORK}/linux-7.3.4"
STAGE_DIR="${IMAGE_WORK}/modules"
if [ -n "$DEVICE_IP" ]; then
    # shellcheck source=../lib/arca-ssh.sh
    . "${SCRIPT_DIR}/../lib/arca-ssh.sh"
    arca_ssh_gate "root@${DEVICE_IP}" || { echo "ERROR: cannot continue without SSH to ${DEVICE_IP}"; exit 1; }
fi

for f in .config Module.symvers vmlinux; do
    [ -f "${KERNEL_DIR}/${f}" ] || { echo "ERROR: ${KERNEL_DIR}/${f} missing; run build-image.sh first."; exit 1; }
done

mkdir -p "$OUT_DIR"
built=0
BUILT_KOS=()
for src in "${SCRIPT_DIR}"/drivers/*/; do
    [ -f "${src}Kbuild" ] || continue
    name="$(basename "$src")"
    rm -rf "${STAGE_DIR:?}/${name}"
    mkdir -p "${STAGE_DIR}/${name}"
    cp "${src}"* "${STAGE_DIR}/${name}/"
    make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE M="${STAGE_DIR}/${name}" modules
    for ko in "${STAGE_DIR}/${name}"/*.ko; do
        cp "$ko" "$OUT_DIR/"
        echo "built: ${OUT_DIR}/$(basename "$ko")  md5 $(md5sum "$ko" | cut -d' ' -f1)"
        BUILT_KOS+=("${OUT_DIR}/$(basename "$ko")")
        built=$((built + 1))
    done
done
[ "$built" -gt 0 ] || { echo "ERROR: no module was built (no kernel/drivers/*/Kbuild?)."; exit 1; }

[ -n "$DEVICE_IP" ] || exit 0
DEVICE="root@${DEVICE_IP}"
# The unit's own release string, so the directory is the one touch-module reads ($(uname -r)/extra).
DEVICE_MODDIR="/lib/modules/$(ssh "$DEVICE" uname -r)/extra"
ssh "$DEVICE" "mkdir -p ${DEVICE_MODDIR}"
for ko in "${BUILT_KOS[@]}"; do
    name="$(basename "$ko")"
    want="$(md5sum "$ko" | cut -d' ' -f1)"
    scp -q "$ko" "${DEVICE}:${DEVICE_MODDIR}/${name}"
    got="$(ssh "$DEVICE" "md5sum ${DEVICE_MODDIR}/${name}" | cut -d' ' -f1)"
    [ "$got" = "$want" ] || { echo "ERROR: ${name} on the unit has md5 ${got}, expected ${want}"; exit 1; }
    echo "deployed: ${DEVICE_MODDIR}/${name}  md5 ${got}"
done
# A loaded touch module cannot be reloaded on 7.3.4 (rmmod then insmod: "irq: type mismatch", measured
# on unit .188), so a new .ko takes effect at the next boot.
echo "A reboot of ${DEVICE_IP} loads the new .ko (touch-module loads it at boot; it does not reload)."
