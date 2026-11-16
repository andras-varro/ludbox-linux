# USB for Arca

> **Scope.** Our 7.3.4 image (`kernel/`) enables MUSB host mode in-kernel (`kernel/patches` 0005/0006) and has the
> pad, USB-audio and USB-storage drivers in-tree as modules, so this component builds and ships **no** `.ko` and no
> `/dev/mem` tool. The vendor 4.14.52 story (runtime `/dev/mem` patch, out-of-tree modules) is deleted; what
> was measured on it survives in `../SYSTEM_ANALYSIS.md#36-usb`.

## What `build-and-deploy.sh` does

```bash
./build-and-deploy.sh <ip>            # install the usb group onto a running unit, no reboot
./build-and-deploy.sh --bundle <dir>  # stage the same into an offline bundle
```

It installs the `usb`-group records of [`../device-files/provision-rules.conf`](../device-files/provision-rules.conf)
(the module loaders `xpad-modules`, `usb-audio-modules`, `usb-storage-modules`, the stick helper `arca-storage` and
their `rc5.d` links), and `fsck.fat` from the Buildroot tree when that source is present. The same bytes arrive through
`provision.sh` and `rootfs/commission-offline.sh`. ⚠️ **A bundle install alone gives no module loaders**:
`deploy-all.sh --from-bundle` installs only what the manifests name; the device scripts and links come from a provision
plan or from this component's `build-and-deploy.sh`.

Prerequisites are those of `../setup-build-env.sh`; the device needs SSH root access and a micro-USB OTG adapter. A
powered hub is optional (`../SYSTEM_ANALYSIS.md#36-usb`).

⚠️ **The module loader's installed name is `/etc/init.d/xpad-modules`; `S89xpad-modules` is the *link*.**
⚠️ **`uImage-system` is never shipped and never written by a script.** `release.sh` refuses to publish any manifest entry whose basename matches `uImage*`.

### Power budget in our own image

Our image's DTB carries `power = <250>` (`0xfa`, 500 mA), set in `usb_otg_hs` of `kernel/dts/rw20.dts`: **measured 2026-10-06 on
`.188`** (an image whose DTB carried the same value from an earlier scripted edit), `/proc/device-tree` reads `0xfa`, and an Xbox 360 pad plugged
directly (no hub) enumerated as `input1` with no `insufficient available bus power` in `dmesg` (operator, n=1).
The vendor image keeps `0x32` (100 mA), where a powered hub is needed; its byte patch is deleted with
vendor-kernel support. `build-and-deploy.sh` prints the live value at the end:
`/proc/device-tree/ocp@68000000/usb_otg_hs@480ab000/power`.



---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| SSH connection fails | Device not on network | Check Ethernet, ping device IP |
| USB device not detected | OTG adapter issue or no VBUS | Use powered USB hub, check adapter polarity |
| Controller in lsusb but no /dev/input | xpad module not loaded | `modprobe xpad` (in-tree `=m` on the 7.3.4 image) |
| No event device after xpad loads | Controller unplugged during load | Replug controller, or unbind/rebind via sysfs |
| **Worked at boot, dead after a replug** | On `.188` a 95 s unplug/replug **works** — VBUS stays up and the disconnect is not processed until the device returns. The `.225` 60 s failure has no confirmed counterpart on current hardware | If it does happen: plug the device in, then Control Panel -> USB -> RESCAN (a musb-hdrc unbind/bind). ⚠️ **`echo host > .../mode` is a silent no-op on this SoC** — `omap2430_ops` has no `.set_mode`. Detail `../SYSTEM_ANALYSIS.md#36-usb` |
| **Nothing enumerates unless it was plugged in at boot** | **A standing property of this hardware, not an open bug** (settled 2026-08-14). MUSB powers the port only when a device is present as the driver probes; otherwise VBUS collapses seconds later and stays off. Three mechanisms read out of the driver source have each been applied and **refuted on hardware** | **Control Panel → USB → RESCAN** — one tap, ~5 s, dead port → playable pad, verified on a panel. Over SSH: `echo musb-hdrc.0.auto > /sys/bus/platform/drivers/musb-hdrc/unbind`, wait 2 s, `bind`, with the device already plugged in. `$MUSB/vbus` is the diagnostic (`Vbus off` = dead port); `mode` is not — it reads `a_idle` while a pad works. `../IMPROVEMENT_PLAN.md` |
| "rejected configuration due to insufficient bus power" | Our DTB keeps the vendor `power` `0x32` (100 mA, inferred from the DTB, not measured on a unit) | Connect the controller through a powered hub |

**Simulating an unplug from sysfs (measured on `.188`):** `echo 1 > /sys/bus/usb/devices/1-1/remove` drops the node (the control panel USB page empties within ~3 s); `authorized=0` on `1-1` does **not** remove it from sysfs or `lsusb`, and toggling `usb1/authorized` did not re-enumerate — a musb-hdrc unbind/bind did (attempt 1).

---

## Failed Approaches (for reference)

| Approach | Why It Failed |
|----------|---------------|
| Vanilla kernel *image* build | **Not a failed approach — it is the current plan**, and no attempt was ever recorded here: "bricked" was a prediction, not an outcome. The policy, the one blocking driver and the per-symbol costs are `../SYSTEM_ANALYSIS.md#7-kernel-policy` |
| EHCI/OHCI alternative | `CONFIG_USB_EHCI_HCD` and `CONFIG_USB_OHCI_HCD` not compiled |
| usbhid for Xbox controller | Controller uses vendor-specific class `ff`, not HID class `03` |
| Runtime DTB power override **via sysfs** | MUSB reads `power` at probe and `hcd->power_budget` is not exposed; there is no sysfs node to write |
| DTB `mode` 3 → 1 (`MUSB_PORT_MODE_HOST`) to revive a cold port | **Refuted on hardware 2026-08-14.** Written to `.188`'s p1 and verified live in the booted tree (`mode` = `00 00 00 01`); a pad plugged in after an empty-socket boot still stayed dark, while a musb-hdrc rebind on the same firmware brought it up on attempt 1. The p1 byte-patch writer that applied it is deleted with vendor-kernel support |
| `echo host > $MUSB/mode` | Silent no-op that returns success: `omap2430_ops` has no `.set_mode` on this SoC |
| Writing `$MUSB/vbus` | Sets `a_wait_bcon`, which nothing on omap2430 reads |
| debugfs `softconnect` | Sets `SESSION` **only** in `OTG_STATE_A_WAIT_BCON`; a cold port sits in `a_idle` |
| A hub left permanently attached (ID-ground) | Measured 2026-08-13: `Vbus off` at 1, 2 and 3 min, and a device plugged into the hub enumerated nothing. An ID event replays the *cached* DEVCTL, and a cold port has no `SESSION` bit to resume |

⚠️ **Require this of a fourth candidate before spending a session on it: how does a port that probed
with an EMPTY socket ever obtain a session?** That is the state every measurement above started from,
and each refuted mechanism silently assumed a session it did not have — which is the single reason all
three failed. A candidate that cannot answer it is the same theory again in new clothes. The answer that
ships is the RESCAN button, verified on a panel; enumeration-at-probe is a standing property of this
hardware with a one-tap remedy, not an open bug.
