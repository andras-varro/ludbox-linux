#!/bin/bash
# Stage the libdbus headers and the shared library to LINK against, from the
# Buildroot tree that built the device's own root filesystem.  Nothing here is
# deployed: the device ships /usr/lib/libdbus-1.so.3 (BlueZ needs it), and that is
# the runtime.  Used by common/avrcp_player.c (the Bluetooth player registration)
# and so by the ludfin binary only.
#
# Usage:
#   ./build-dbus-deps.sh                     # stage into arm-deps-softfp/ unless already there
#   ./build-dbus-deps.sh --force             # restage
#   ./build-dbus-deps.sh --prefix            # print the sysroot path, copy nothing
#   ./build-dbus-deps.sh --from <dir>        # Buildroot staging dir (default $BR_STAGING,
#                                            # else ~/br-rw/staging)
#
# Output: arm-deps-softfp/usr/include/dbus-1.0/dbus/*.h,
# arm-deps-softfp/usr/lib/dbus-1.0/include/dbus/dbus-arch-deps.h and
# arm-deps-softfp/usr/lib/libdbus-1.so.3 plus libdbus-1.so.  The unversioned name is
# a COPY, not a symlink (a DrvFs checkout cannot hold symlinks); the linker reads
# the soname out of it, so -ldbus-1 records DT_NEEDED libdbus-1.so.3.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/arm-deps-softfp"
STAGING="${BR_STAGING:-$HOME/br-rw/staging}"

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) echo "$PREFIX"; exit 0 ;;
        --force)  rm -rf "${PREFIX:?}/usr/include/dbus-1.0" "${PREFIX:?}/usr/lib/dbus-1.0" \
                         "$PREFIX/usr/lib/libdbus-1.so" "$PREFIX/usr/lib/libdbus-1.so.3"; shift ;;
        --from)   STAGING="${2:?--from needs a directory}"; shift 2 ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | sed '$d;s/^# \{0,1\}//'; exit 0 ;;
        *) echo "Usage: $0 [--force | --prefix | --from <staging dir> | --help]"; exit 1 ;;
    esac
done

# The artifacts, not a stamp.
if [ -f "$PREFIX/usr/lib/libdbus-1.so" ] && [ -f "$PREFIX/usr/lib/libdbus-1.so.3" ] \
   && [ -f "$PREFIX/usr/include/dbus-1.0/dbus/dbus.h" ] \
   && [ -f "$PREFIX/usr/lib/dbus-1.0/include/dbus/dbus-arch-deps.h" ]; then
    echo "libdbus link files already staged ($PREFIX/usr/lib/libdbus-1.so)"
    exit 0
fi

for f in usr/include/dbus-1.0/dbus/dbus.h usr/lib/dbus-1.0/include/dbus/dbus-arch-deps.h usr/lib/libdbus-1.so.3; do
    if [ ! -f "$STAGING/$f" ]; then
        echo "$STAGING/$f not found."
        echo "That is the Buildroot staging dir of the root filesystem build:"
        echo "run rootfs/build-rootfs.sh (it builds ~/br-rw), or point BR_STAGING / --from at one."
        exit 1
    fi
done

mkdir -p "$PREFIX/usr/include/dbus-1.0/dbus" "$PREFIX/usr/lib/dbus-1.0/include/dbus"
cp -L "$STAGING"/usr/include/dbus-1.0/dbus/*.h "$PREFIX/usr/include/dbus-1.0/dbus/"
cp -L "$STAGING/usr/lib/dbus-1.0/include/dbus/dbus-arch-deps.h" "$PREFIX/usr/lib/dbus-1.0/include/dbus/"
cp -L "$STAGING/usr/lib/libdbus-1.so.3" "$PREFIX/usr/lib/libdbus-1.so.3"
cp -L "$STAGING/usr/lib/libdbus-1.so.3" "$PREFIX/usr/lib/libdbus-1.so"
echo "libdbus staged from $STAGING into $PREFIX"
