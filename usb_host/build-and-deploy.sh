#!/bin/bash
# build-and-deploy.sh — USB host mode, Xbox controller and USB audio support for Arca
#
# Usage:
#   ./build-and-deploy.sh                          # build the artifacts only
#   ./build-and-deploy.sh <ip>                     # build + deploy
#   ./build-and-deploy.sh --bundle <dir>           # build + stage into an offline bundle
#
# Prerequisites — all of the host ones come from ../setup-build-env.sh, which carries the
# one package set for the whole repo. This script only reports what it is missing:
#   - arm-linux-gnueabihf-gcc
#   - bc, libssl-dev, bison, flex               (kernel module build, first run only)
#   - SSH key auth to root@<ip>                 (deploy only)
#
# ── TWO mechanisms, TWO homes, neither on p1 ───────────────────────────────
#
#   1. device-files/provision-rules.conf's `usb` group — the module-loader and
#      storage scripts. Host mode itself is enabled in-kernel by the 7.3.4 image.
#   2. The pad, USB-audio and USB-storage drivers are in-tree modules of the 7.3.4 image
#      (kernel/build-image.sh modules_install; loaded by modprobe from the device scripts),
#      so this component builds and ships no .ko.
#   3. fsck.fat, from the Buildroot tree (~/br-rw/target/sbin/fsck.fat), stripped
#      into fsck-staging/ and installed to /sbin/fsck.fat. arca-storage runs it
#      before mounting a stick. Optional: when the source is absent this script
#      warns and goes on without it.
#
# So this script restates neither. It BUILDS the artifacts, runs the ARM
# gate over them, and then drives the same implementations every other path
# drives.

# Source-path directive: resolves the source= hints below against this script's directory.
# shellcheck source-path=SCRIPTDIR
set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# The two shared libraries every path needs: the SSH gate, and ../lib/arca-bundle.sh —
# --bundle stages through it AND a deploy clears the provenance stamp through it.
# shellcheck source=../lib/arca-ssh.sh
. "$REPO_ROOT/lib/arca-ssh.sh"
# shellcheck source=../lib/product.sh
. "$REPO_ROOT/lib/product.sh"
# shellcheck source=../lib/arca-bundle.sh
. "$REPO_ROOT/lib/arca-bundle.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}" >&2; exit 1; }
ts()   { echo "[$(date '+%H:%M:%S')] $*"; }

usage() {
    echo "Usage: $0 [<ip>]"
    echo "       $0 --bundle <dir>"
    echo ""
    echo "  <ip>             Device IPv4 address; omit to build without deploying"
    echo "  --bundle <dir>   Build, then stage the artifacts under <dir>/root/"
    echo "                   with a declared-mode manifest. No device needed."
    echo ""
    echo "  The four device scripts and the three rc5.d links are NOT installed from"
    echo "  here in isolation — they are device-files/provision-rules.conf's \`usb\`"
    echo "  group, and this script runs that same plan. ./provision.sh"
    echo "  <ip> and commission-offline.sh install it too, from the same records."
    exit 1
}

# ── arguments ───────────────────────────────────────────────────────────────
BUNDLE_DIR=""
DEVICE_IP=""

if [[ "${1:-}" == "--bundle" ]]; then
    BUNDLE_DIR="${2:-}"
    [[ -n "$BUNDLE_DIR" ]] || { echo "--bundle requires a directory"; echo ""; usage; }
    [[ -z "${3:-}" ]] || { echo "Unexpected argument after --bundle <dir>: $3"; exit 1; }
else
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)      usage ;;
            -*)             echo "Unknown option: $1"; echo ""; usage ;;
            *)
                [[ -z "$DEVICE_IP" ]] || { echo "Unexpected argument: $1"; echo ""; usage; }
                DEVICE_IP="$1"; shift ;;
        esac
    done
    # Validated before building, not at the first ssh: this script builds kernel
    # modules first, which takes minutes.
    IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
    if [[ -n "$DEVICE_IP" && ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
        echo "Not an IPv4 address: $DEVICE_IP"; echo ""; usage
    fi
fi
DEVICE="root@$DEVICE_IP"

# ── the built artifacts, declared ONCE ─────────────────────────────────
#
# "<mode>|<local path>|<device path>". Both the --bundle staging and the scp
# deploy read this array, so a new artifact is added in one place — the same rule
# native_apps' GAMES_BINARIES follows. Modes are DECLARED, never read off disk:
# /mnt/c reports every file 0777 and discards chmod (../CLAUDE.md).
#
USB_ARTIFACTS=()   # fsck.fat is appended below when Buildroot has built it


# fsck.fat is built by Buildroot (rootfs/), not by this component. The copy shipped is
# STRIPPED, staged under fsck-staging/ so the Buildroot tree is never touched. The ARM gate
# reads the UNSTRIPPED source instead (see step 3). Absent source is a warning, not a failure.
FSCK_SRC="$HOME/br-rw/target/sbin/fsck.fat"
FSCK_STRIP="$HOME/br-rw/host/bin/arm-buildroot-linux-gnueabi-strip"
FSCK_STAGED="$SCRIPT_DIR/fsck-staging/fsck.fat"
if [[ -f "$FSCK_SRC" && -x "$FSCK_STRIP" ]]; then
    mkdir -p "$SCRIPT_DIR/fsck-staging"
    cp -f "$FSCK_SRC" "$FSCK_STAGED"
    "$FSCK_STRIP" "$FSCK_STAGED"
    USB_ARTIFACTS+=("0755|$FSCK_STAGED|/sbin/fsck.fat")
else
    warn "fsck.fat not found at $FSCK_SRC (or no strip tool at $FSCK_STRIP) — skipping it; arca-storage mounts without the pre-mount check"
fi

echo ""
echo "════════════════════════════════════════"
echo " $PRODUCT_NAME USB Host + Controller + Audio"
echo "════════════════════════════════════════"
ts "Started — $(date '+%Y-%m-%d %H:%M:%S')"
[[ -n "$BUNDLE_DIR" ]] && info "Staging a bundle: $BUNDLE_DIR"
[[ -n "$DEVICE_IP" ]] && info "Target: $DEVICE"
echo ""

# ── 0. prerequisites ────────────────────────────────────────────────────────
ts "[0/7] Prerequisites"
command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 \
    || err "arm-linux-gnueabihf-gcc not found. Install every host prerequisite with setup-build-env.sh, at the repo root."
ok "arm-linux-gnueabihf-gcc"

if [[ -n "$DEVICE_IP" ]]; then
    # The shared gate (../lib/arca-ssh.sh): it tells "down" from "up and
    # refusing us" and, on a terminal, offers to install a key.
    arca_ssh_gate "$DEVICE" || err "Cannot continue without SSH to $DEVICE"
    ok "SSH to $DEVICE"

    # Locally built artifacts, so any provenance stamp on the unit is about to stop
    # being true. One shared writer (../lib/arca-bundle.sh), before anything goes out.
    arca_bundle_clear_stamp "$DEVICE" \
        || warn "could not clear /opt/arca/bundle.info — it may still name an older release"
fi
echo ""


# ── 3. the ARM-safety gate, on every built artifact ─────────────────────────────────────
#
# Cortex-A8 has no hardware integer divide: an sdiv/udiv INSTRUCTION is SIGILL
# (exit 132) with a blank screen and no log. Runs before the deploy AND before
# --bundle, for the same reason vnc_client's does — a published bundle is installed
# by someone with no toolchain.
#
# ⚠️ Exit 2 ("could not judge") is a REFUSAL here, unlike in
# commission-offline.sh. Every usb_host artifact is unstripped by construction —
# `$CC -static` with no strip, and the module build does not strip either — and
# fsck.fat is gated at its unstripped Buildroot source, not at the stripped staging
# copy, so a 2 means the build changed, not that this component ships stripped binaries
# the way scummvm and vnc_client do. Keeping it fatal is what makes "usb_host contributes
# zero TAKEN ON TRUST entries to a bundle" a checked property.
#
# ⚠️ The status is read directly, NEVER through xargs: xargs collapses any exit of
# 1–125 onto its own 123 and erases the difference between "a real hit" and "could
# not judge".
ts "[3/7] ARM-safety gate (no sdiv/udiv)"
ARM_TARGETS=()
for a in "${USB_ARTIFACTS[@]}"; do
    [[ "$a" == *"|$FSCK_STAGED|"* ]] && continue   # the stripped copy; gated unstripped below
    ARM_TARGETS+=("${a#*|}"); ARM_TARGETS[-1]="${ARM_TARGETS[-1]%%|*}"
done
[[ -f "$FSCK_SRC" ]] && ARM_TARGETS+=("$FSCK_SRC")
if [[ ${#ARM_TARGETS[@]} -eq 0 ]]; then
    # check-arm-safe.sh with no arguments falls back to build/* of the current directory: never ask it that
    ok "no ARM artifact to gate (fsck.fat absent)"
elif [[ -x "$REPO_ROOT/native_apps/check-arm-safe.sh" || -f "$REPO_ROOT/native_apps/check-arm-safe.sh" ]]; then
    arm_rc=0
    bash "$REPO_ROOT/native_apps/check-arm-safe.sh" "${ARM_TARGETS[@]}" || arm_rc=$?
    case "$arm_rc" in
        0) ok "hard zero across ${#ARM_TARGETS[@]} artifact(s)" ;;
        2) err "the gate could not judge one of these artifacts — it is STRIPPED.
     Every usb_host artifact is unstripped by construction, so this means the
     build changed. objdump reads Thumb-2 as ARM without a symbol table and
     invents sdiv/udiv, so there is no sound verdict to be had on a stripped
     file. Do not deploy or bundle these." ;;
        *) err "an artifact would SIGILL on this device — refusing to deploy or bundle" ;;
    esac
else
    err "../native_apps/check-arm-safe.sh is missing — refusing to ship ungated ARM binaries"
fi
echo ""

# ── 4. --bundle: stage and stop ─────────────────────────────────────────────
if [[ -n "$BUNDLE_DIR" ]]; then
    ts "[4/7] Staging → $BUNDLE_DIR"
    arca_bundle_init "$BUNDLE_DIR" usb_host || err "could not prepare $BUNDLE_DIR"
    for a in "${USB_ARTIFACTS[@]}"; do
        mode="${a%%|*}"; rest="${a#*|}"; src="${rest%%|*}"; dev="${rest#*|}"
        arca_bundle_add "$BUNDLE_DIR" usb_host "$mode" "$src" "$dev" \
            || err "staging failed: $dev"
    done
    warn "The device scripts are provision-rules.conf's, not the bundle's"
    ok "Staged $(arca_bundle_finish "$BUNDLE_DIR" usb_host) file(s)"
    echo ""
    exit 0
fi

if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — built only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    exit 0
fi

# ── 4. the `usb` group of the provision plan ────────────────────────────────
#
# ⚠️ The decisions are NOT here. The device scripts, their modes and the two
# rc5.d links are records in device-files/provision-rules.conf, read by this
# script, by provision.sh and by commission-offline.sh — so the three
# paths cannot drift. The executor is the SAME
# generated interpreter provision.sh pipes to the device.
#
# What used to be here: three scp calls, three chmod +x, two `ln -sf` and an
# /etc/init.d name (S89xpad-modules) that matched nothing else in the repo.
ts "[4/7] Device scripts and boot links (provision-rules.conf, group usb)"
# shellcheck source=../lib/arca-identify.sh
. "$REPO_ROOT/lib/arca-identify.sh"
# shellcheck source=../lib/arca-provision.sh
. "$REPO_ROOT/lib/arca-provision.sh"

PROV_RULES="$REPO_ROOT/device-files/provision-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(arca_provision_validate "$PROV_RULES" "$REPO_ROOT")"; then
    echo "$PCHECK"; err "provision-rules.conf does not validate"
fi

USB_PLAN=$(mktemp)
trap 'rm -f "$USB_PLAN"' EXIT INT TERM
arca_provision_plan_component "$PROV_RULES" usb > "$USB_PLAN" \
    || err "could not compile the usb provision plan"
info "$(arca_provision_plan_summary "$USB_PLAN")"

# `install` is the one verb the remote interpreter cannot do alone: the source
# bytes are on this host, so they go over scp first and it only sets the mode.
# ⚠️ The loop is lib/arca-provision.sh's, not a copy, and history says why: this script and
# provision.sh each had one, both reading the plan on stdin with an
# `ssh` in the body, and both therefore installed exactly one file.
arca_provision_push_installs "$USB_PLAN" "$REPO_ROOT" "$DEVICE" \
    || err "could not copy the usb provision sources to the device"

ssh "$DEVICE" "cat > /tmp/arca-usb-plan" < "$USB_PLAN"
arca_provision_online_script | ssh "$DEVICE" "cat > /tmp/arca-usb-provision.sh"
ssh "$DEVICE" "sh /tmp/arca-usb-provision.sh /tmp/arca-usb-plan; rc=\$?; rm -f /tmp/arca-usb-provision.sh /tmp/arca-usb-plan; exit \$rc" \
    || err "the usb provision step failed on the device"
ok "scripts installed, S88usb-storage-modules and S89xpad-modules linked"
echo ""

# ── 5. the built artifacts ─────────────────────────────────────────────
ts "[5/7] Built artifacts"
ssh "$DEVICE" "mkdir -p /usr/local/bin"
for a in "${USB_ARTIFACTS[@]}"; do
    mode="${a%%|*}"; rest="${a#*|}"; src="${rest%%|*}"; dev="${rest#*|}"
    scp -q "$src" "$DEVICE:$dev" || err "could not copy $src to $dev"
    ssh "$DEVICE" "chmod $mode '$dev'"
    ok "$(basename "$src") → $dev ($mode)"
done
ok "depmod"
echo ""

# ── 6. bring it up now, without a reboot ────────────────────────────────────
#
# The module loads are runtime state, so they can be done immediately — which is
# what makes the dev loop one command, with no reboot. Host mode itself is
# enabled in-kernel by the 7.3.4 image (kernel/patches), so there is nothing to
# start for it. A port that came up dead is revived from the Control Panel USB
# page (RESCAN), with the device plugged in first.
ts "[6/7] Loading the module services now"
ssh "$DEVICE" "/etc/init.d/xpad-modules start" || warn "module load reported a failure"
ssh "$DEVICE" "/etc/init.d/usb-audio-modules start" || warn "usb-audio module load reported a failure"
ssh "$DEVICE" "/etc/init.d/usb-storage-modules start" || warn "usb-storage module load reported a failure"
echo ""

# ── 7. verify ────────────────────────────────────────────────────────────────
ts "[7/7] Verifying on the device"
echo ""
ssh "$DEVICE" sh -s <<'VERIFY'
echo "--- USB bus ---"
if [ -d /sys/bus/usb/devices/usb1 ]; then
    echo "  host mode: ACTIVE"
else
    echo "  host mode: NOT ACTIVE"
fi

echo ""
echo "--- VBUS (the reading that says whether the port is alive) ---"
VB=/sys/devices/platform/68000000.ocp/480ab000.usb_otg_hs/musb-hdrc.0.auto/vbus
if [ -r "$VB" ]; then
    echo "  $(cat "$VB" 2>/dev/null)"
    echo "  'Vbus off' means nothing plugged in later will be seen — plug the"
    echo "  device in and rescan from the Control Panel USB page."
else
    echo "  unknown — MUSB not bound"
fi

echo ""
echo "--- power budget (live device tree) ---"
POWER=$(hexdump -e '4/1 "%02x"' /proc/device-tree/ocp*/usb_otg_hs*/power 2>/dev/null)
case "$POWER" in
    000000fa) echo "  500 mA (0xfa) — a controller works with no powered hub" ;;
    00000032) echo "  100 mA (0x32) — a controller needs a POWERED hub" ;;
    *)        echo "  unknown ($POWER)" ;;
esac

echo ""
echo "--- modules ---"
lsmod 2>/dev/null | grep -E "^Module|xpad|joydev|ff_memless|usb_storage" || echo "  (none)"

echo ""
echo "--- USB devices ---"
lsusb 2>/dev/null || echo "  (lsusb not available)"

echo ""
echo "--- input devices ---"
for ev in /dev/input/event*; do
    [ -e "$ev" ] || continue
    echo "  $ev: $(cat "/sys/class/input/$(basename "$ev")/device/name" 2>/dev/null)"
done

echo ""
echo "--- boot links ---"
for l in /etc/rc5.d/S88usb-storage-modules /etc/rc5.d/S89xpad-modules; do
    if [ -L "$l" ]; then
        echo "  $l -> $(readlink "$l")"
    else
        echo "  $l MISSING"
    fi
done
VERIFY

echo ""
_ELAPSED=$(( $(date +%s) - _START_SECONDS ))
echo "════════════════════════════════════════"
printf "  Done — %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo "════════════════════════════════════════"
echo ""
