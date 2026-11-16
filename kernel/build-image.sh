#!/bin/bash
# build-image.sh - Build a bootable uImage and its modules from the vanilla Linux 7.3.4 tree for Arca
# Run this script in WSL (Linux). The recipe and the reasoning behind each step are in
# kernel/README.md.
#
# Prerequisites: every host package this needs comes from ../setup-build-env.sh, the one
# home for them (the cross-compiler is its `core` group; mkimage its `kmod` group). The device
# tree is compiled by the kernel tree's own scripts/dtc, so it needs nothing more.
#
# Produces: <out>/zImage-with-dtb, <out>/uImage-test, <out>/modules/ (modules_install tree) and <out>/modules-extra/
# (our out-of-tree drivers). It never writes p1: installing the image as uImage-system is a manual
# operator step (kernel/README.md), and nothing scripted writes it.

# shellcheck source-path=SCRIPTDIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# The tarball's pin lives in rootfs/package-hashes/linux-headers.hash; the check runs before tar xf.
# shellcheck source=../lib/arca-kernel-src.sh
. "${REPO_DIR}/lib/arca-kernel-src.sh"

KERNEL_VERSION="7.3.4"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_VERSION}.tar.xz"
# The vendor 4.14.52 config: compared against the new .config only, to list dropped symbols.
DEVICE_CONFIG="${REPO_DIR}/usb_host/device_config"
PATCH_DIR="${SCRIPT_DIR}/patches"
BASE_CONFIG="${SCRIPT_DIR}/config-${KERNEL_VERSION}"
# Our own board description (kernel/dts/rw20.dts), compiled against the tree's mainline dtsi files.
DTS_SOURCE="${SCRIPT_DIR}/dts/rw20.dts"
DTS_TREE_DIR="arch/arm/boot/dts/ti/omap"   # where the tree keeps OMAP3 boards; rw20.dts lands there as omap3-rw20.dts
CROSS_COMPILE="arm-linux-gnueabihf-"
ARCH="arm"

WORK_DIR="${HOME}/rw-kbuild-73"
OUT_DIR=""
DTB=""
DTB_MODE="build"
REUSE=0
INITRD_ROOT="${HOME}/br-rw/target"   # rootfs/build-rootfs.sh BUILD_DIR/target: where busybox and glibc come from
INITRD_SRC="${SCRIPT_DIR}/initramfs/init"

usage() {
    cat <<'USAGE'
Usage: kernel/build-image.sh [--work <dir>] [--out <dir>] [--dtb <file>] [--initrd-root <dir>] [--reuse]

Fetch linux-7.3.4.tar.xz (sha256-checked against rootfs/package-hashes/linux-headers.hash) into
the work dir, extract a fresh tree, apply kernel/patches/*.patch in sorted order, configure from
kernel/config-7.3.4 + olddefconfig, build zImage and every module, append the DTB and wrap it
with mkimage into uImage-test. Stage that under a NEW name on p1. Then build kernel/drivers/*
out of tree (build-modules.sh) into <out>/modules-extra.

The DTB, by default: kernel/dts/rw20.dts, copied into the tree as ti/omap/omap3-rw20.dts and
built there, so the tree's own cpp include path and scripts/dtc apply.

  --work <dir>   Build tree parent, in the WSL filesystem (default: ~/rw-kbuild-73).
                 Refused under /mnt: a kernel build on DrvFs is many times slower.
  --out <dir>    Where zImage-with-dtb and uImage-test go (default: the work dir).
  --dtb <file>   Append this DTB instead of building rw20.dts.
  --initrd-root <dir>  Buildroot target tree that supplies busybox and glibc for the initramfs
                 (default: ~/br-rw/target, the tree rootfs/build-rootfs.sh builds).
  --reuse        Keep an existing tree, applying only patches not yet in it. Fast rebuilds.
  -h, --help     This text.

Writes <work>/dropped-symbols.txt: every symbol =y/=m in the vendor 4.14.52 device_config that
the new .config does not carry as =y/=m. Nothing in the repo is written. Timestamps are pinned
to the repo's last commit, so the same inputs give the same uImage. Modules are stamped with
the release string 7.3.4, so they load only on an image from this script, never on another build.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --work)       shift; WORK_DIR="${1:?--work needs a directory}" ;;
        --out)        shift; OUT_DIR="${1:?--out needs a directory}" ;;
        --dtb)        shift; DTB="${1:?--dtb needs a file}"; DTB_MODE="file" ;;
        --initrd-root) shift; INITRD_ROOT="${1:?--initrd-root needs a directory}" ;;
        --reuse)      REUSE=1 ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done
OUT_DIR="${OUT_DIR:-$WORK_DIR}"
KERNEL_DIR="${WORK_DIR}/linux-${KERNEL_VERSION}"
KERNEL_TARBALL="${WORK_DIR}/linux-${KERNEL_VERSION}.tar.xz"

echo "[0/7] Checking prerequisites..."
missing=0
for tool in "${CROSS_COMPILE}gcc" "${CROSS_COMPILE}strip" mkimage bc bison flex curl; do
    command -v "$tool" &>/dev/null || { echo "ERROR: ${tool} not found."; missing=1; }
done
if [ "$missing" -ne 0 ]; then
    echo "  Install them with: ./setup-build-env.sh --install-deps   (kmod group: mkimage, bc, bison, flex)"
    exit 1
fi
# tracked in git (the unit's own /proc/config.gz), so only a damaged checkout lacks it.
if [ ! -f "$DEVICE_CONFIG" ]; then
    echo "ERROR: ${DEVICE_CONFIG} not found (it is tracked in git: git checkout -- usb_host/device_config)."
    echo "  To replace it from a unit, in usb_host/: scp root@<ip>:/proc/config.gz device_config.gz && gunzip device_config.gz"
    exit 1
fi
[ "$DTB_MODE" = file ] || [ -f "$DTS_SOURCE" ] || { echo "ERROR: ${DTS_SOURCE} not found."; exit 1; }
case "$(realpath -m "$WORK_DIR")" in
    /mnt/*) echo "ERROR: work dir ${WORK_DIR} is on DrvFs. Use a path under \$HOME."; exit 1 ;;
esac
mapfile -t PATCHES < <(find "$PATCH_DIR" -maxdepth 1 -name '*.patch' | sort)
[ "${#PATCHES[@]}" -gt 0 ] || { echo "ERROR: no patches in ${PATCH_DIR} (the image boots with no network without them)."; exit 1; }
# Pin every timestamp and build-identity string the image embeds, so the same inputs give the
# same bytes: the kernel's UTS version string (read literally), and mkimage's header time.
EPOCH=$(git -C "$REPO_DIR" log -1 --format=%ct 2>/dev/null || stat -c %Y "$KERNEL_TARBALL")
KBUILD_BUILD_TIMESTAMP="$(date -u -d "@${EPOCH}")"
export SOURCE_DATE_EPOCH="$EPOCH" KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=arca KBUILD_BUILD_HOST=build
echo "  Cross compiler: $(${CROSS_COMPILE}gcc --version | head -1)"
echo "  Build epoch:    ${EPOCH} (${KBUILD_BUILD_TIMESTAMP})"

# The device tree is compiled in step 5, before the kernel build, because it needs the tree
# and its .config; only the choice is made here.
echo; echo "[1/7] Device tree (${DTB_MODE})..."
mkdir -p "$WORK_DIR"
case "$DTB_MODE" in
    file)   [ -f "$DTB" ] || { echo "ERROR: DTB ${DTB} not found."; exit 1; }
            DTB_SOURCE="--dtb ${DTB}" ;;
    build)  DTB="${KERNEL_DIR}/${DTS_TREE_DIR}/omap3-rw20.dtb"
            DTB_SOURCE="kernel/dts/rw20.dts, built in the tree" ;;
esac

echo; echo "[2/7] Extracting kernel source..."
if [ "$REUSE" -eq 1 ] && [ -f "${KERNEL_DIR}/Makefile" ]; then
    echo "  --reuse: keeping ${KERNEL_DIR}"
else
    rm -rf "$KERNEL_DIR"
    if [ ! -f "$KERNEL_TARBALL" ]; then
        echo "  Downloading ${KERNEL_URL}"
        if curl -fL --retry 3 -o "${KERNEL_TARBALL}.part" "$KERNEL_URL"; then
            mv "${KERNEL_TARBALL}.part" "$KERNEL_TARBALL"
        else
            echo "ERROR: download failed: ${KERNEL_URL}"; exit 1
        fi
    fi
    arca_check_kernel_tarball "$KERNEL_TARBALL" || exit 1
    tar xf "$KERNEL_TARBALL" -C "$WORK_DIR"
    echo "  Extracted into ${KERNEL_DIR}"
fi

# A patch counts as applied only when it reverses cleanly afterwards. Under --reuse a patch that
# already reverses is skipped; one that neither reverses nor applies was edited since, so refuse.
# ⚠️ The reverse probe needs --forward: `-R --batch` on an UNpatched tree prints "Unreversed patch
# detected! Ignoring -R." and exits 0 (measured), so every check here would pass vacuously.
echo; echo "[3/7] Applying ${#PATCHES[@]} patch(es) from ${PATCH_DIR}..."
for p in "${PATCHES[@]}"; do
    if patch -d "$KERNEL_DIR" -p1 -R --forward --dry-run -s < "$p" &>/dev/null; then
        if [ "$REUSE" -eq 0 ]; then echo "ERROR: $(basename "$p") reverses on a fresh tree."; exit 1; fi
        echo "  Already applied: $(basename "$p")"
        continue
    fi
    if ! patch -d "$KERNEL_DIR" -p1 --forward --batch --no-backup-if-mismatch < "$p"; then
        echo "ERROR: $(basename "$p") did not apply."
        [ "$REUSE" -eq 0 ] || echo "  Under --reuse a changed patch needs a fresh tree: drop --reuse."
        exit 1
    fi
    patch -d "$KERNEL_DIR" -p1 -R --forward --dry-run -s < "$p" >/dev/null \
        || { echo "ERROR: $(basename "$p") reported success but does not reverse."; exit 1; }
    echo "  Applied: $(basename "$p")"
done

echo; echo "[4/7] Configuring from kernel/config-${KERNEL_VERSION} + olddefconfig..."
# The initramfs: kernel/initramfs/init is the trampoline (it switch_roots into p3 unless the cmdline has arca_root=0);
# busybox (stripped) and the glibc runtime come from the Buildroot target tree, so no binary is in
# the repo. The list is generated here, with paths into the work dir, and handed to the kernel
# through CONFIG_INITRAMFS_SOURCE (config-7.3.4 keeps it empty).
INITRD_DIR="${WORK_DIR}/initramfs"
INITRD_LIST="${INITRD_DIR}/initramfs.list"
stage_initramfs() {
    local l r="${INITRD_DIR}/rootfs"
    [ -f "$INITRD_SRC" ] || { echo "ERROR: ${INITRD_SRC} not found."; exit 1; }
    [ -x "${INITRD_ROOT}/bin/busybox" ] || { echo "ERROR: ${INITRD_ROOT}/bin/busybox not found: build the root filesystem first (rootfs/build-rootfs.sh) or pass --initrd-root."; exit 1; }
    rm -rf "$INITRD_DIR"
    mkdir -p "${r}/bin" "${r}/lib"
    "${CROSS_COMPILE}strip" -o "${r}/bin/busybox" "${INITRD_ROOT}/bin/busybox"
    for l in ld-linux.so.3 libc.so.6 libm.so.6 libresolv.so.2 libdl.so.2 libpthread.so.0 librt.so.1; do
        [ -e "${INITRD_ROOT}/lib/${l}" ] || { echo "ERROR: ${INITRD_ROOT}/lib/${l} not found."; exit 1; }
        cp -aL "${INITRD_ROOT}/lib/${l}" "${r}/lib/${l}"
    done
    cp "$INITRD_SRC" "${r}/init"
    {
        echo "dir /dev 755 0 0"; echo "nod /dev/console 600 0 0 c 5 1"
        for d in sbin proc tmp lib mnt sys bin etc root; do echo "dir /${d} 755 0 0"; done
        echo "file /init ${r}/init 755 0 0"
        for l in ld-linux.so.3 libc.so.6 libm.so.6 libresolv.so.2 libdl.so.2 libpthread.so.0 librt.so.1; do
            echo "file /lib/${l} ${r}/lib/${l} 755 0 0"
        done
        echo "file /bin/busybox ${r}/bin/busybox 755 0 0"
        echo "slink /bin/sh busybox 777 0 0"
    } > "$INITRD_LIST"
    echo "  Initramfs list: ${INITRD_LIST} ($(wc -l < "$INITRD_LIST") entries)"
}
stage_initramfs
cp "$BASE_CONFIG" "${KERNEL_DIR}/.config"
# A tarball tree has no .git, so the release string is 7.3.4 either way; pinning LOCALVERSION off keeps
# it so if a git tree is ever used, and the release string is the vermagic every .ko is stamped with.
"${KERNEL_DIR}/scripts/config" --file "${KERNEL_DIR}/.config" --disable LOCALVERSION_AUTO --set-str LOCALVERSION "" \
    --set-str INITRAMFS_SOURCE "$INITRD_LIST"
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE olddefconfig
# olddefconfig drops a symbol whose dependency is missing, silently; these are the ones the board
# cannot do without (display on omapfb, not DRM; USB host; modules with versioned symbols).
for want in CONFIG_FB_OMAP2=y CONFIG_FB_OMAP2_PANEL_DPI=y CONFIG_USB_MUSB_HDRC=y CONFIG_MODULES=y \
            CONFIG_MODVERSIONS=y CONFIG_SERIAL_OMAP_CONSOLE=y CONFIG_JOYSTICK_XPAD=m; do
    grep -qxF "$want" "${KERNEL_DIR}/.config" \
        || { echo "ERROR: .config lacks '${want}' after olddefconfig"; exit 1; }
done
if grep -qE '^CONFIG_DRM_OMAP=[ym]' "${KERNEL_DIR}/.config"; then
    echo "ERROR: CONFIG_DRM_OMAP is set: it would claim the display the omapfb driver needs"; exit 1
fi
# Lowercase is legal in a symbol name (CONFIG_VFPv3), so the class is [A-Za-z0-9_].
sym_re='^CONFIG_[A-Za-z0-9_]+=[ym]$'
DROPPED="${WORK_DIR}/dropped-symbols.txt"
comm -23 <(grep -E "$sym_re" "$DEVICE_CONFIG" | cut -d= -f1 | sort -u) \
         <(grep -E "$sym_re" "${KERNEL_DIR}/.config" | cut -d= -f1 | sort -u) > "$DROPPED"
echo "  Vendor 4.14.52 symbols the new config lacks ($(wc -l < "$DROPPED"), saved to ${DROPPED}); mostly renames, review pending:"
sed 's/^/    /' "$DROPPED"

echo; echo "[5/7] Building the device tree, zImage and modules with -j$(nproc)..."
# The tree's own rule for a .dtb (cpp with the tree's include path, then scripts/dtc), run on a
# copy of rw20.dts dropped in beside the mainline dtsi files it includes. Before the kernel build,
# so a DTS error costs seconds. Always copied: under --reuse the tree may hold an older rw20.dts.
if [ "$DTB_MODE" = build ]; then
    cp "$DTS_SOURCE" "${KERNEL_DIR}/${DTS_TREE_DIR}/omap3-rw20.dts"
    rm -f "$DTB"
    make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE ti/omap/omap3-rw20.dtb
    [ -s "$DTB" ] || { echo "ERROR: omap3-rw20.dtb was not produced."; exit 1; }
    echo "  Built: $(basename "$DTB") ($(stat -c %s "$DTB") bytes)"
fi
start=$SECONDS
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j"$(nproc)" zImage
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j"$(nproc)" modules
echo "  zImage built in $((SECONDS - start)) s."

# -C none, never -C gzip: -C describes the blob handed over, and a zImage self-extracts.
echo; echo "[6/7] Appending DTB and wrapping with mkimage..."
mkdir -p "$OUT_DIR"
ZIMAGE="${KERNEL_DIR}/arch/arm/boot/zImage"
cat "$ZIMAGE" "$DTB" > "${OUT_DIR}/zImage-with-dtb"
mkimage -A arm -O linux -T kernel -C none -a 0x80008000 -e 0x80008000 -n '' \
        -d "${OUT_DIR}/zImage-with-dtb" "${OUT_DIR}/uImage-test"

echo; echo "  Image built. DTB source: ${DTB_SOURCE}"
echo "  md5sums:"
md5sum "$ZIMAGE" "$DTB" "${OUT_DIR}/uImage-test" | sed 's/^/    /'
echo "  Install by hand: stage uImage-test on p1 under a NEW name and boot that first (kernel/README.md); the .ko files must come from this same build."

echo; echo "[7/7] Installing modules and building kernel/drivers/*..."
rm -rf "${OUT_DIR}/modules" "${OUT_DIR}/modules-extra"
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules_install \
     INSTALL_MOD_PATH="${OUT_DIR}/modules" INSTALL_MOD_STRIP=1 DEPMOD=true
bash "${SCRIPT_DIR}/build-modules.sh" --out "${OUT_DIR}/modules-extra" --image "$WORK_DIR"
echo "  Module tree: ${OUT_DIR}/modules/lib/modules/$(cat "${KERNEL_DIR}/include/config/kernel.release") (run depmod -b on it, or on the unit)"
