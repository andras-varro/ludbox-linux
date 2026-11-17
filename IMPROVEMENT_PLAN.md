# Improvement Plan

**Open work only.** Finished items are not listed here — the code and `git log` are the record.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup. IDs are never reused or
  renumbered, because commit messages cite them. ⚠️ **Nothing outside this file may cite one** — an entry
  is deleted the moment it closes, so any citation elsewhere has a scheduled expiry built in
  (`tests/doc_check.sh` group B counts them, resolving or not). To find a closed one: `git log --grep=B13i`.
- **Status is one word after the heading:**

  | Status | Means |
  |---|---|
  | `open` | Nothing has shipped. Found by reading the code; not reproduced on the device. |
  | `open, confirmed <date>` | Reproduced — on the panel or by running the command. Still unfixed. |

- **A recorded cause and a prescribed fix are both hypotheses.** `open, confirmed` means the
  *symptom* reproduced and nothing more. Reproduce it, then find the cause yourself; and when an
  entry says "compare with X", read X. See `CLAUDE.md` → *Working style*.
- Kernel work is in scope; the image is `kernel/build-image.sh` on mainline 7.3.4. What stays excluded is in [Out of Scope](#out-of-scope).

**Before starting anything, read [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md) §1 — *Read this first*.**
Device facts live there; this file holds only what we intend to *do* about them.

---

## Needs a human at the panel

**Nothing is outstanding.** Past an app's first screen a check is scripted with `arca_tap` (`CLAUDE.md` → *Non-obvious
constraints*); what still needs a human is what an eye or a finger judges, and panel time is the project's scarce
resource: price the check before requesting it, group requests into **one checklist**, split an item when
only part of it is gated, and record the answer with the confidence it was given — "I think it works" is a
hedge, and a verdict is a weaker answer than a description, so say which one the next item needs.

---

## Correctness and verification

### B30. `brick_breaker` hides lives past the ninth — open, latent, cosmetic

Three games draw a capped HUD lives row and each caps it differently:

| Game | Shape | Verdict |
|---|---|---|
| `frogger.c:1269` | `lives_shown = min(lives, 5)`, then lays out from `lives_shown * LIFE_ICON_PITCH` | correct |
| `platformer.c:1541` | lays out from the capped number and draws one icon plus `x10` past the cap | correct |
| `brick_breaker.c:1398` | `for (i < game.lives && i < 9)`, each heart at a fixed anchor minus `i * 14` | **silently truncates** |

The extra-life power-up at `brick_breaker.c:765` does `game.lives++` with no cap, so a tenth life and
every one after it is **invisible**: the HUD reads nine whether you hold nine or fourteen, and losing one
appears to change nothing. Cosmetic, never a crash, and it needs a power-up-heavy run to reach, which is
why it has not been seen.

Fix is the rule the other two follow: cap first, lay out from the capped number, and make the number
appear somewhere once it exceeds what is drawn — a heart plus `x10`, or a raised cap.

### B41. An adapter replug can print `configured as A device timeout` on our image — open, seen once 2026-09-29

**Measured on `.188`** (uImage md5 `f3b446c6b2d731e0d118583cada62c36`, the ID-ground patch booted):
`musb-hdrc musb-hdrc.0.auto: configured as A device timeout` was printed once, at one adapter replug —
the one following a spell in which the root hub had been runtime-suspended; the earlier replug did not
print it. Enumeration succeeded anyway. The line comes from `omap2430_musb_set_vbus()`, which waits for
DEVCTL `BDEVICE` to clear — now reached on every ID-ground, since the patch calls it there. **Read from
source, not timed on the device:** upstream's loop is 100 × `mdelay(5)`, so it gave up after ~505 ms, before
the 1 s jiffies deadline it appeared to have, then carried on as on success. Console error lines are defects
even when harmless. **[inferred]** that the prior root-hub suspend is the difference; n=1. **Watch:** the
message now reads `configured as A device timeout: devctl %02x after %lld ms[ (irq)]` (`kernel/patches/musb-omap2430-set-vbus-report.patch`),
so the next occurrence measures itself; read the devctl value and elapsed ms there before reproducing, then
decide whether the wait needs the PHY/glue resumed first or the loop is simply too short.

### B69. Does the SD slot's card-detect switch work, and is it on the pad the DT names? — open, unverified

`kernel/dts/rw20.dts:571` has `cd-gpios = <&gpio4 31 GPIO_ACTIVE_LOW>` (gpio_127, the vendor's value, kept) while the pad it muxes as a GPIO (`rw20.dts:286`) is `sdmmc1_dat4` = gpio_126. **[unverified]** Whether the switch is read at all has not been checked; the card boots, so nothing observed depends on it. **Done when** a card insert and removal is observed in the kernel log, or the property is shown to be inert.

### B76. A plain `reboot` can hang in the shutdown scripts, and `provision.sh` reboots that way — open, seen once on `.115`

**Measured 2026-10-31 on `.115`, n=1:** two `reboot` calls over SSH left the unit up; `/etc/init.d/rc 0` was asleep in
`wait_woken` with fd 0–2 on `/dev/console` (a VT, 100×30, not the serial port), so the K scripts never ran (`sshd` still up).
**Inferred:** a write to the console blocked. `sync; reboot -f` booted. `provision.sh` ends with a plain `ssh … reboot`,
so an update can silently not take effect. **Next:** reproduce, read which K script or `rc` line writes, and why a VT write
blocks (console scrolled-lock? `fbcon` blanked?). **Done when** a plain `reboot` reboots `.115` five times in a row from the
launcher state, or `provision.sh` detects a reboot that did not happen. **Not reproduced** in 4 tries the same day (idle, just
booted, during `rc 5`, two `reboot`s at once). Next capture before `reboot -f`: `/proc/<rc pid>/stack`, `stty -F /dev/console -a`
and `stty -F /dev/ttyO1 -a`; `rc` echoes to the console (serial `ttyO1` + `tty0`) before each K script [inferred first write].

## Features

Userspace, except the kernel-image entry.

---

### F170. 7.3.4 image: what is still open — open

The image is `kernel/build-image.sh` with `kernel/config-7.3.4` and `kernel/patches/`; its state is the table in [`kernel/README.md`](kernel/README.md#driver-state-on-734). Left, none measured on 7.3.4 yet: **(a) boot logo** — render `distro/arca-logo.svg` to a ≤224-colour PPM and add a `kernel/patches/` entry replacing `drivers/video/logo/logo_linux_clut224.ppm` (`LOGO_LINUX_CLUT224` is enabled), padded or full-frame because the logo draws at the top-left with no centring **[inferred from 4.14 `fbmem.c`, not re-read on 7.3.4]**; **(b) USB gadget** — `CONFIG_USB_GADGET` is off (measured in the config), and `PREEMPT`/`HZ=250` are off too, never shown to limit anything. A kernel change is proven by booting it; take a verified p1 backup of the *running* image first ([`#4-boot-chain-and-recovery`](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). **Done when** each lettered item is measured on a unit or dropped.

### F137. Users other than root — open, operator wish 2026-10-09, not designed

The operator wants root not to be the default user on our root filesystem. Operator's answer, 2026-10-10: a normal user and
root; normal operation runs as the user; a change such as the SSH settings needs root (type the root password) and then runs as
root, like `sudo`. That touches `/dev/fb0`, `/dev/input`, `/dev/dsp`, `/dev/watchdog` and the `arca-app` respawn loop. If it
is too much effort it goes last. The terminal app (`native_apps/terminal/`) runs `/bin/login`, so it grants exactly what the accounts grant. **Done when** it is designed together with the operator and built.

### F145. Update a unit from a bundle — open, operator request 2026-10-10, not designed

Two package kinds: a ready-to-use generic card image (`.img`) and a bundle update. The bundle update installs only what changed
(per component), and the upstream it fetches from is configurable (today `deploy-all.sh --from-release` fixes one). A bundle-based
update normally avoids reflashing, so the unit keeps its host keys. **Done when** a unit updates from a bundle with only the changed
components installed, from a configured upstream.

### F160. Does BT/USB audio need the speaker's `>>1` ceiling? — open, operator decision 2026-10-24

`AUDIO_MASTER_SHIFT` (`common/audio_gen.h`, set in `common/audio.c`) halves every output: it is the internal speaker's ceiling,
but it is spent on BT and USB sinks too, which is 6 dB of headroom they may not need. The system volume's top step (+12 dB,
soft-limited) already sounds right on a BT speaker [operator, by ear]. **Next:** an A/B on one BT and one USB sink, shift 1 vs 0
at the same volume step, listening for clipping and limiter pumping. **Done when** the shift per sink class is decided and, if it
differs, set by the output route rather than the app.

### F106. Support BeagleBone Black boards — open, operator idea 2026-10-01, future

The operator inherited many BeagleBone Black boards (photos: `beaglebone/image/beaglebone-black-board-{1,2}.jpg`). **Measured from the
photos:** TI AM3358 (Cortex-A8, 1 GHz — also no hardware integer divide), TPS65217C PMIC, 512 MB DDR3,
eMMC, SMSC LAN8710A PHY, an NXP HDMI framer to micro-HDMI, USB-A host, mini-USB, microSD, P8/P9 headers.

**Inferred, not built:** our apps need only `/dev/fb0`, evdev and ALSA, so the differences are board-specific —
no touch panel, other LED/backlight/watchdog paths, no speaker GPIO or bezel, 5 V power. That points to a
**board-profile layer** under `common/hardware.c`, `common/touch_input.c` and `provision.sh` rather than
`#ifdef`s. Our binaries are soft-float against the Arca rootfs while stock BBB Debian is hard-float, so
this ties to our own root filesystem (`rootfs/`; one rootfs for both boards). A BBB wants a mainline kernel with DRM fbdev emulation; the
Steelcase RoomWizard stays on omapfb, which the 7.3.4 image keeps (`panel-dpi`, per `kernel/README.md`). **Done when** the first question is answered: which board-specific
paths a launcher plus one game actually touch, listed from a BBB boot.

**Future scope, operator-agreed, not scheduled:** mouse-only operation of the launcher, Control Panel and games, for a unit with no touch and no keyboard or pad. The operator has a RoomWizard whose touch is broken, and a BeagleBone has no touch. The mouse pointer in the launcher and the Control Panel exists and is operator-verified; the games still take no mouse pointer.

### Potential ideas (not planned)

Recorded so they are not lost; none has an owner or a design.

- Meeting or home dashboard: clock, weather and a calendar (ICS) feed, with the red and green LEDs as busy and free, the device's original role.
- Home Assistant or MQTT control panel: buttons on the touch screen, state shown on the LEDs. Operator decision: Alexa integration is not a goal (no public local API).
- Notification display: an HTTP push (for example ntfy) shown on screen with an LED flash.
- Touch drawing board or whiteboard.
- Ambient sound or white-noise generator on `common/audio_gen.c`.

## Structural and cleanup

### C10. Make a deep game state reachable without playing to it — open

`brick_breaker`'s indestructible bricks only exist from **level 5 up**, so verifying them costs a full
play session of somebody's time. A `--level N` argument or a debug entry in the pause dialog turns it into
one launch, and would serve any future level-dependent bug; generalise to the other games where a state is
expensive to reach. **Operator ruling: a test-only command-line switch is acceptable** — the launcher
passes no arguments, so such a flag is reachable over SSH and deliberately not from the panel, which is
what a test entry point wants. `brick_breaker` already has both halves (`--test` and a pause toggle), so
its level-5 problem is open on the level number, not on the mechanism. A pause-dialog toggle (the shape
Office Runner's TRAINING toggle in `platformer.c` uses) is reachable by `arca_tap` taps, so a mode
with no CLI entry is still script-checkable; a CLI switch only makes the deep state cheaper to reach.

### C15. The bare plan-ID scan collides with function-key names — open, measured 2026-09-03

`bare_sites()` in `tests/doc_check.sh` matches an `F`-numbered ID in parentheses or after `see`/`is`/
`was`, and F1-F12 are key names as well as plan IDs here — a ScummVM key-table row such as
`Save/load dialog (F5)` counts as a citation, and it turns into a dangling one the moment an entry of that
number closes. **So the reported citation count carries false positives, and the next F-numbered entry to
close can fail the gate on unrelated documentation.** Narrow the scan rather than excluding a file: a hit
on a line that also carries a key-binding marker (`Ctrl+`, `Alt+`, `Shift+`), or one inside a two-column
key table, is not a citation. ⚠️ Needs a control in both directions — a real bare citation must still fire,
and it must fire in a file of the same kind, or the scan goes blind where it used to see.

---

## Out of Scope

Recorded so the decision is not re-litigated. Anything needing a connector, a populated device or a wire
is out under [`#8-hardware-policy`](SYSTEM_ANALYSIS.md#8-hardware-policy); a kernel config symbol on its own
blocks nothing, since we build the image. Requesting GPL source from Steelcase stays ruled out and
is needed for nothing ([`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)).

| Item | Blocked by | Detail |
|------|---|---|
| Enable the two EHCI USB host ports | **Nowhere to plug in** — no second USB connector and no unpopulated footprint on the board, so `CONFIG_USB_EHCI_HCD` is not what blocks this and an image we build gains nothing here | [`#36-usb`](SYSTEM_ANALYSIS.md#36-usb) |
| SPI | Four controllers `okay` in the DT and `CONFIG_SPI` unset, but **nothing is on the bus** — no children are declared, and putting a device there needs a wire | [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present) |
| Piezo buzzer on TWL4030 PWM | **Needs a wire**, and all 3 dmtimer PWMs are taken; `CONFIG_PWM_TWL` is the cheap half | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| `omapdrm` instead of `omapfb` | **Operator decision: the 7.3.4 image keeps `omapfb`** (`panel-dpi`, [`kernel/README.md`](kernel/README.md#driver-state-on-734)); `omapdrm` would break the runtime bpp switching and the DSS overlay sysfs | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |
| Ambient-light sensor / auto-backlight | **No such hardware.** The teardown found no sensor and, decisively, no aperture, window or light pipe anywhere in the enclosure — a sensor would have nothing to sense even if fitted. ⚠️ Do **not** probe for it: `pv02_app 5` can hang I2C bus 1, which carries the PMIC. *Time-of-day* dimming needs no sensor and is still available. | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Serial console | Located and pinned out (`P4`), then declined: the recovery loop is *pull the card, reimage, DHCP, SSH*, and since NAND and U-Boot stay untouched the card **is** the entire failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or U-Boot ever get written — or once we are iterating on our own images (F101), where serial is the only channel that shows *why* one failed to boot, though fitting `P4` is itself a board change. | [`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports) |

**Note:** enabling **UART3** as a `ttyO2` is *not* in this table — it may be reachable by patching the
appended DTB, which needs no kernel source ([`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports)).

⚠️ **Operator ruling, 2026-09-06: no further USB work beyond USB audio.** Enumeration-at-probe is closed
and that is a result rather than a gap — three mechanisms read out of the MUSB driver were each applied
and **refuted on hardware**, and the answer that ships is the one-tap RESCAN, verified on a panel. Before
anyone proposes a fourth theory, read the refuted table in `usb_host/README.md`, which names the one
never-attempted candidate and the question any candidate must answer first. **The operator ruling of 2026-10-15 lifts this
for USB storage** (the file manager and the Backup page); the enumeration-at-probe closure above stands.

**Bundles hold built artifacts only — settled**, because the one consumer that installs device scripts runs
from a clone and has `device-files/` beside it either way.
