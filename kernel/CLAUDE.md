# kernel/ — our Linux 7.3.4 image, its patches and its drivers

Everything that goes **into the kernel image** lives here: the source patches, the device-tree changes,
the build script, and the out-of-tree driver work. The walkthrough and the working notes are
[`README.md`](README.md). Facts about the device, including what the vendor kernel does, go in
`SYSTEM_ANALYSIS.md` (policy and dropped-driver table: [§7](../SYSTEM_ANALYSIS.md#7-kernel-policy)).
Open work goes in `IMPROVEMENT_PLAN.md`. Neither is restated here.

## Rules before the first edit

- ⚠️ **Never build in, or patch, `usb_host/linux-4.14.52/`.** That vanilla vendor-era tree is no build input any more: no script reads it. The image is
  built by `build-image.sh` from the pinned `linux-7.3.4.tar.xz` in a fresh tree under WSL `$HOME` (`~/rw-kbuild-73`), never
  on `/mnt/c`, because DrvFs makes a kernel build crawl.
- **A patch here is GPL-2.0-only and applies with `patch -p1` from the kernel tree root**, in sorted
  filename order. A new patch needs no registration; `build-image.sh` applies every `patches/*.patch`
  and refuses to continue if one fails.
- ⚠️ **`olddefconfig` drops a symbol silently**, and a dropped driver fails at runtime, not
  at build time: no panel, dead touch. `build-image.sh` asserts the symbols the board needs and writes `dropped-symbols.txt`
  (the vendor `usb_host/device_config` symbols the new config lacks, mostly renames). Read it after any config change; the
  review of the current list is pending (`README.md`).
- ⚠️ **Modules load only on the image built by the same script and config**: vermagic is `7.3.4` (`LOCALVERSION_AUTO` off) and
  `MODVERSIONS` is on, so any change to a patch or the config means rebuilding every module.
- ⚠️ **A kernel change is proven only by booting it**, and writing `uImage-system` is governed by the p1 rules in
  `lib/CLAUDE.md`. Take a verified backup of the *running* image first. `ssh … reboot` of a unit under test works and is
  authorised when the operator has named the unit; otherwise ask the operator to reboot and name the unit.
- **Reading the vendor binary beats guessing.** The vendor kernel's `/proc/kallsyms` against our
  `System.map`, compared per-function by size, names every function the vendor patched. The method is
  in [`README.md`](README.md#reading-the-vendor-kernel).
- **The initramfs trampoline is `initramfs/init`**: a busybox script that `switch_root`s into p3 on `arca_root=1`. `build-image.sh` generates the list; busybox and glibc come from the Buildroot target tree, so commit no binary there.
- `tools/` holds **userspace** probes built for the device (`-static`, no idiv). They are not kernel
  code, so `native_apps/check-arm-safe.sh` applies to them too. `drivers/<name>/` holds out-of-tree
  modules, built by `build-modules.sh` against our image's tree and loadable only on our image.
- `3rdparty/<vendor>/<subsystem>/` holds **unmodified** vendor blobs (firmware), each beside its licence
  text and a `README.md` with measured provenance. Never edit or compress one; replace it from upstream.
