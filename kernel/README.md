# kernel/ — building our own Linux 7.3.4 image

The working notes for the kernel we build ourselves: how the image is produced, what we patch and why, the
state of each driver, and the method that read the vendor kernel. **Device facts found here move to
`SYSTEM_ANALYSIS.md`**: the policy is in [§7](../SYSTEM_ANALYSIS.md#7-kernel-policy), the panel timings in
[§3.2](../SYSTEM_ANALYSIS.md#32-display), the touch register map in [§3.3](../SYSTEM_ANALYSIS.md#33-touch).
Open work is in `IMPROVEMENT_PLAN.md`. Rules for editing this directory: [`CLAUDE.md`](CLAUDE.md).

Every measurement below was taken on unit `.188` (n=1 unit, a serial header attached) from 2026-11-14 to
2026-11-15, as a test kernel booted from p1, unless it says otherwise. The image is **not** installed as
`uImage-system` on any unit.

| Path | What it is |
|---|---|
| `build-image.sh` | download `linux-7.3.4.tar.xz` (sha256 pinned in `rootfs/package-hashes/linux-headers.hash`) → patch → configure → `zImage` + modules + DTB → `uImage-test` → modules_install → `build-modules.sh`; `--help` is current |
| `patches/*.patch` | source patches against Linux 7.3.4, `patch -p1` from the tree root in sorted order, GPL-2.0-only; the device tree is **not** a patch |
| `dts/rw20.dts` | our board's device tree for the 7.3.4 tree, GPL-2.0-only; the build copies it to `arch/arm/boot/dts/ti/omap/omap3-rw20.dts` and builds it with the tree's own cpp and `scripts/dtc` |
| `config-7.3.4` | the full `.config` (not a fragment): omapfb on, `DRM_OMAP` off, `MODULES`/`MODVERSIONS` on, `JOYSTICK_XPAD=m` |
| `drivers/cy8ctmg120_ts/` | out-of-tree touch driver (`.c` + `Kbuild`), adapted from vanilla `cy8ctmg110_ts.c`; binds the `panjit_ts` DT node |
| `build-modules.sh` | builds every `drivers/*/` with a `Kbuild` via `M=` against the tree `build-image.sh` left in `~/rw-kbuild-73`; loads only on that image |
| (no module recipe) | BT, USB audio, USB storage, AX88179 and xpad modules are in-tree `=m` in `config-7.3.4` and come from `build-image.sh`'s modules_install; `cy8ctmg120_ts` is the only out-of-tree driver |
| `tools/i2c_touch_read.c` | userspace burst reader for the touch controller over `/dev/i2c-N`; it never writes to the part |

## Building an image

```bash
wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && kernel/build-image.sh --out <scratch>/img"
```

`<scratch>` is any directory outside the repo. The script works in `~/rw-kbuild-73` (refused on `/mnt`), extracts
a fresh tree, applies `patches/`, stages the initramfs, copies `config-7.3.4`, forces `LOCALVERSION_AUTO` off and runs `olddefconfig`,
then asserts the symbols the board cannot do without (`FB_OMAP2`, `MUSB_HDRC`, `MODULES`, `JOYSTICK_XPAD=m`, ...) and
that `DRM_OMAP` is off. `dropped-symbols.txt` lists every vendor `usb_host/device_config` symbol the new config
lacks, mostly renames; **reviewing it is pending [unverified]**. The wrap is
`cat zImage board.dtb > zImage-with-dtb` then `mkimage -A arm -O linux -T kernel -C none -a 0x80008000 -e 0x80008000`.
A full build was not run by this repo's gate; the patch set applying in order to the pinned tarball was **measured**
(a fresh extraction, `patch -p1` per patch in sorted order, 2026-11-15).

- ⚠️ **`-C none`, never `-C gzip`**: `-C` describes the blob handed to `mkimage`, and a `zImage` decompresses itself.
- **The release string is `7.3.4`** (no `-dirty`, no git hash): `LOCALVERSION_AUTO` is off, and it is the vermagic every
  `.ko` is stamped with. ⚠️ **Modules load only on an image built by this script from the same config**; a rebuilt
  `vmlinux` with other options changes `struct module` or CRCs and the `.ko` is refused (`struct module size`,
  `disagrees about version of symbol`). The 4.14 `.ko` files are refused the same way on 7.3.4 [measured].
- **Timestamps and identity are pinned** (`SOURCE_DATE_EPOCH` = last commit), so equal inputs give an equal `uImage`.
- **Boot recipe tested** [measured on `.188`]: the U-Boot appends no bootargs; the test cmdline was
  `console=ttyO1,115200n8 vram=12M consoleblank=0 clocksource=32k_counter ignore_loglevel`, set from the serial U-Boot
  prompt (`fatload` the `uImage-test*` file, `setenv bootargs`, `bootm`), and the card's U-Boot passes no `arca_root`.
  **The image carries an initramfs** (`kernel/initramfs/init`, a busybox trampoline): it waits for `mmcblk0p3`, mounts it rw,
  and `switch_root`s into `/sbin/init` unless the cmdline has `arca_root=0` (then it logs to p1 and leaves a serial shell); a failed hand-off also leaves the shell.
  `build-image.sh` generates the list in `<work>/initramfs/` and sets `CONFIG_INITRAMFS_SOURCE` to it (the config keeps it empty);
  busybox (stripped) and the glibc runtime are copied from the Buildroot target tree (`--initrd-root`, default `~/br-rw/target`),
  so no binary is committed. ⚠️ **Never put `root=/dev/mmcblk0p3` on the cmdline**: `growroot` would resize p3.
- **Installing is a manual operator step** and nothing scripted writes `uImage-system` ([`lib/CLAUDE.md`](../lib/CLAUDE.md)). Stage the
  image on p1 under a **new** name; a kernel that loads and hangs does not fall back, so with `uEnv.txt` `kernel=`
  recovery is a card reader. The serial-prompt route is one-shot and undone by a power cycle. `build-image.sh` itself
  writes only `uImage-test`.
- **A reboot of a unit under test is fine when the operator named the unit** (`ssh root@<ip> reboot` ran and was not refused,
  `.188`, 2026-11-14).

## What we patch, and why

| Patch | Without it | Status |
|---|---|---|
| `0001-smsc911x-reset-pulse` | no network: the DT marks the LAN9221 reset active-high; vanilla requests it low and never pulses it | eth0 up, DHCP [measured] |
| `0002-ti-bandgap-unreliable-info` | the `ti-soc-thermal` "unreliable sensor" warning at warning level on every boot | the sensor itself is still **unreliable [unverified]** |
| `0003-musb-host-flush-gone-device` | `musb_h_tx_flush_fifo` retries 1000 x 1 ms with IRQs off behind an unplugged hub | measured on 4.14 (n=5); on 7.3.4 not re-timed [unverified] |
| `0004-musb-omap2430-release-mstandby-on-init` | with Inventra DMA, the glue's force-standby bit survives a rebind and the DMA channel stalls | behaviour on 7.3.4 not re-measured [unverified] |
| `0005-musb-omap2430-host-session-on-id-ground` | the host-only kernel never binds a gadget, so a plugged adapter never writes `SESSION`; the first version (v1) still failed on hot-plug: the PHY/ID path needs time to settle after a plug, so the mailbox's `ID_GROUND` arm calls `musb_set_host()` in a retry loop (up to 8 x `msleep(500)`) | **measured on `.188` only**: boot-plugged `DEVCTL 0x5D`; unplug then replug re-enumerates by itself; empty-socket boot then later hub plug-in works |
| `0006-musb-a-idle-disconnect` | a clean unplug hits the `default:` arm of the `DISCONNECT` switch, `is_active` stays set | rebased; **measured on `.188` only**, together with 0005 |
| `0007-omapfb-panel-dpi-data-lines` | `panel_dpi_probe_of` never reads `data-lines`; fb0 is sized for 16 bpp | panel `800x480` 32 bpp lit [measured] |
| `0008-omapdss-honour-syncclk-active` | DT cannot express this panel's sync-on-falling, data-on-rising edges | applied, panel lit [measured] |
| `0009-omapdss-scale-mode-param` | the DSS overlay scaler tables blur 2x upscales; adds `omapdss.scale_mode` | applies; the scaler is deliberately unused ([§3.2](../SYSTEM_ANALYSIS.md#32-display)) |
| `0010-omapfb-mmap-map-count-double-inc` | `omapfb_mmap()` increments `rg->map_count` twice and `munmap` decrements once, so after any fb mmap `OMAP-FB SETUP_MEM` returns `EBUSY` and the retro cores run audio only | measured on `.188`: retro runs, `626x480` overlay, 61 fps (operator saw the picture) |
| `0011-btusb-asus-1bf6-realtek` | the ASUS USB-BT500 dongle `0b05:1bf6` (RTL8761CU) is not in btusb's table, so it never reaches `btrtl`; 7.3.4's `btrtl` already handles the chip | applies to v7.3.4 after 0001-0010; `btusb` ID only, firmware from `3rdparty/realtek/bluetooth/` [dry-run only, not tested on a unit] |
| `0012-logo-arca-clut224` | replaces the Tux boot logo (`logo_linux_clut224.ppm`) with the Arca cuckoo, 80x80, flat colours (218) on black; `CONFIG_LOGO_LINUX_CLUT224` is the only logo enabled (MONO and VGA16 are off) | applies to v7.3.4 after 0001-0011 (`patch --dry-run`); text PPM, no binary ; operator saw it on the panel of a unit booting it (2026-11-17) |

Dropped from the 4.14 set: the twelve TCP patches (upstream in 7.3.4 [grep]), `musb-omap2430-set-vbus-report` (its hook is gone) and
`leds-pwm-dt-brightness` (the DT's `default-state`/`default-brightness` replace it).

**Device tree facts that are not patches (`dts/rw20.dts`).**

- ⚠️ **`&timer12 { /delete-property/ ti,timer-alwon; };` is required.** Without it the boot hangs right after the dmtimer
  clocksource registers (`probe of 48304000.timer ... returned 0`, even with `clocksource=32k_counter`, because
  `timer-ti-dm.c` also takes over `sched_clock`). **Measured by A/B: that line was the only change.** Why timer12 does not count on this part is
  **inferred** (a GP part's secure timer).
- The backlight LED carries `default-state = "on"` and `default-brightness = <100>`: the sysfs value reads 100 after boot [measured]; the
  panel by eye was not confirmed. Apps still apply the user's `backlight_brightness` themselves.
- `gpmc,cycle2cycle-diffcsen` and `-samecsen` are bare booleans (the warning is gone, behaviour identical).
- `usb_otg_hs` keeps `power = <250>` (500 mA budget) and `mode = <3>`; the `power` child of `twl@48` (`ti,system-power-controller`) for software
  power-off; `mmc1` `disable-wp`; the LAN9221 GPMC timings. All were measured on 4.14 ([§3.6](../SYSTEM_ANALYSIS.md#36-usb)); on 7.3.4 **not
  re-measured** [unverified].

## Driver state on 7.3.4

| Function | State |
|---|---|
| Serial, i2c, twl4030, SD, ethernet | boot and work [measured, `.188`] |
| Panel | omapfb with `panel-dpi`; picture and console visible (operator) |
| Touch | module `cy8ctmg120_ts.ko`, **loaded at boot** by `device-files/touch-module` from `/lib/modules/$(uname -r)/extra`; operator confirmed touch. ⚠️ **`rmmod` then `insmod` fails** (`irq: type mismatch`, `no IRQ in the DT node`): reboot, never reload. The only source change from 4.14 is the `probe` signature |
| USB host | works incl. hot-plug and empty-socket boot (patches 0005, 0006) |
| Bluetooth, USB audio, AX88179 | load as modules from the same build (`CONFIG_UHID=m` and `uhid` in `device-files/bluetooth` for BlueZ's `/dev/uhid` input; BlueZ 5.66 default, from memory [unverified]); BT reconnect, A2DP, USB audio and a controller worked (operator). `bluetoothd` died once with no trace (cause unknown, **[inferred]** a crash on link loss); the `bluetooth` script now restarts it |
| Xbox pad | `xpad`, `ff-memless`, `joydev` are `=m` in `config-7.3.4`; modprobe on the unit **not tested live** [unverified] |
| Retro, ScummVM, Jellyfin, VNC | operator confirmed |
| sshd | ⚠️ the rootfs `sshd-session` was killed by its own seccomp sandbox on syscall 403 (`clock_gettime64`) under 7.3.4 when built against 4.14 headers; the unit ran a hand-patched binary. The build now uses 7.3.4 headers and keeps the OpenSSH patch; **measured by the operator: root ssh login works on the v3.0.0 card** (see `rootfs/CLAUDE.md`) |
| Suspend and power management | not supported, not tested (operator, 2026-11-16); shutdown measured: backlight, side LEDs, NIC and USB go off |
| SD card-detect | `kernel/dts/rw20.dts` has `cd-gpios = <&gpio4 31 GPIO_ACTIVE_LOW>` (gpio_127, the vendor value) while the muxed pad is `sdmmc1_dat4` = gpio_126; whether the switch is read is **not measured [unverified]** and does not matter: the card is behind the enclosure and the unit boots from it (operator, 2026-11-17) |

## Reading the vendor kernel

The vendor source is not available, and that has not mattered. The vendor's `/proc/kallsyms` is compared with a
vanilla `System.map` of the same release (4.14.52), each function's size being the distance to the next symbol. Functions whose size differs, or
that exist only in the vendor image, are what it patched; each is disassembled from the decompressed vendor
`Image` with its `bl` targets and literal-pool strings resolved. That found the Ethernet reset pulse and the
whole panel driver: timings, GPIO order, delays, and the signal polarity sysfs never exposed. The tools are scratch-grade and not in the
repo (`fsize.py`, `calls.py`, `dis.sh`, `sharp_dis.py`; data `vendor-Image`, `vendor.kallsyms`).

## Spectre

The SoC core is a Cortex-A8 r1p7 (measured). The BTB flush on a context switch does nothing unless `ACTLR.IBE` is set; the kernel's IBE write
is compiled out under `ARCH_MULTIPLATFORM`. `ACTLR` read `0x000000e2` on `.188` under the 4.14 image (bootloader-set [inferred]) using
`drivers/ca8_ibe`. **Not re-measured on 7.3.4 [unverified]**; `ca8_ibe` is an instrument, loaded by hand, never at boot.

## Building the out-of-tree drivers

After `build-image.sh`: `wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && kernel/build-modules.sh --out <scratch>/ko [--deploy <ip>]"`.
Building `tools/i2c_touch_read.c`: `arm-linux-gnueabihf-gcc -O2 -static -o i2c_touch_read i2c_touch_read.c`, then run
`native_apps/check-arm-safe.sh` on the result. ⚠️ **`scp` does not preserve the exec bit**; and stop whatever owns the screen before an
`--evdev` witness (`app_launcher` may hold an `EVIOCGRAB`). ⚠️ **Any change to an image patch or the config means rebuilding every module**
(`MODVERSIONS=y`).
