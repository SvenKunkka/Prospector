# Build and flash — Keychron K10 Max (ANSI, RGB)

## 1. Build

```bash
export QMK_HOME=$HOME/qmk_firmware          # Keychron fork, branch 2025q3
cd "$QMK_HOME"

# Prospector build (this is the requested command)
qmk compile -kb keychron/k10_max/ansi/rgb -km keychron

# Stock build for comparison / rollback (module compiled out)
qmk compile -kb keychron/k10_max/ansi/rgb -km default
```

Verified on this machine with `arm-none-eabi-gcc 15.2.1`:

| Build | Size (`.bin`) | Result |
|---|---|---|
| `-km keychron` (Prospector, transport=rawhid) | 77 228 B | success |
| `-km default` (stock, module OFF) | 72 396 B | success |
| `-km keychron` with `PROSPECTOR_TRANSPORT=uart` | 78 812 B | success |

Give a different keymap name if you do not want to touch
`keymaps/keychron/rules.mk`; the build is gated entirely by `PROSPECTOR_ENABLE`.

## 2. Output files

```
$QMK_HOME/.build/keychron_k10_max_ansi_rgb_keychron.bin     <- flash this
$QMK_HOME/.build/keychron_k10_max_ansi_rgb_keychron.hex
$QMK_HOME/keychron_k10_max_ansi_rgb_keychron.bin            <- qmk copies it here too
```

A copy of the built artifact is staged in `firmware/` in this deliverable.

## 3. Bootloader

Confirmed from the board definition, not guessed:

```console
$ python3 -c "import json;d=json.load(open('keyboards/keychron/k10_max/info.json'));\
  print(d['bootloader'], d['processor'], d['usb'])"
stm32-dfu STM32F401 {'vid': '0x3434', 'pid': '0x0AA0'}
```

So this is the **STM32 factory ROM DFU bootloader**, which enumerates as
`0483:DF11` (STMicroelectronics) — the keyboard's own `3434:0AA0` disappears
while it is in the bootloader. That matches what was observed on this unit
earlier (STM32 ROM DFU at `0483:DF11`, serial `345B36873234`, 256 KB internal
flash reported by `alt=0`).

### Entering it

`features.bootmagic` is `true` for this board, so:

1. Unplug USB.
2. Hold the **Esc** key (top-left, matrix row 0 col 0).
3. Plug USB in while holding it, then release.

Confirm before flashing:

```bash
dfu-util -l          # expect: Found DFU: [0483:df11] ... "@Internal Flash /0x08000000/..."
```

If nothing appears, re-try with the cable in a different port, and make sure
nothing on the board is being held that would put it in 2.4 GHz / BT mode.

## 4. Flash

```bash
# QMK's own path (wraps dfu-util with the right address and :leave)
qmk flash -kb keychron/k10_max/ansi/rgb -km keychron
```

Or directly, if you prefer to see exactly what is written:

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave \
         -D "$QMK_HOME/.build/keychron_k10_max_ansi_rgb_keychron.bin"
```

`:leave` tells the ROM bootloader to start the application when done. If it
does not restart, unplug and replug — that always works, because a normal
power-up runs the application.

## 5. Recovery

The stock image is one command away, and that is the point of building
`-km default` before flashing anything:

```bash
qmk compile -kb keychron/k10_max/ansi/rgb -km default
qmk flash   -kb keychron/k10_max/ansi/rgb -km default
```

**Note on the unit's current state.** When this work was done the keyboard was
not attached to this machine: `system_profiler SPUSBDataType` showed no
`3434:0AA0` and no `0483:DF11`, and `/dev/cu.*` had no keyboard port. An
earlier session left it sitting in the ROM DFU bootloader and it would not
detach back to the application. A plain power-cycle is the first thing to try;
only if the application still does not come up is a write needed, and the image
to write is the stock `-km default` build above.

## 6. What flashing does and does not change

Changed: the STM32F401 application image only.

Not changed:

- the **LKBT51 wireless module** and its firmware (nothing here writes to it),
- the module's pairing/bonding state and the keyboard's Bluetooth host slots,
- VIA / Keychron Launcher behaviour (`VIA_ENABLE = yes` is kept),
- NKRO, RGB, sleep/wake, USB, Bluetooth, 2.4 GHz, factory test,
- the official keymap — the module hooks into keyboard-level callbacks, so the
  keymap itself is byte-identical to stock.

## 7. Console / debug logging

```make
PROSPECTOR_DEBUG = yes
```

in the keymap's `rules.mk` enables `CONSOLE_ENABLE` and prints through
Keychron's own console (`kc_printf` → `dprintf`):

```
[Prospector] init, transport=rawhid
[Prospector] change -> burst (layer=2)
[Prospector] ver=2.2.3 bat=88% layer=2 prof=0 conn=1 flags=0x30 role=0 mods=0x00 wpm=0 ch=0 name=L2   id=DEADBEEF -> rawhid
```

---

## 8. Flash log (what actually happened)

Unit serial `345B36873234`.

| # | Action | Command | Result |
|---|---|---|---|
| 1 | Enumerate DFU | `dfu-util -l` | `[0483:df11]`, `alt=0 @Internal Flash /0x08000000/04*016Kg,01*064Kg,01*128Kg` = 256 KB, serial `345B36873234` |
| 2 | **Backup (read-only)** | `dfu-util -a 0 -s 0x08000000:262144 -U …` | success, 262 144 B. SHA-256 `ff0426833ede5dead8db3f1f2752a581fed73950dc292f55447fc8072fd14601` — **identical to the earlier session's backup**, so the flash had not changed in between. Staged as `firmware/k10max_FACTORY_original_256k.bin`. First contact reported `dfuERROR / "Device's firmware is corrupt"`; the read cleared it to `dfuIDLE`. |
| 3 | Write | `dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D firmware/k10max_prospector_rawhid.bin` | erase + download, 109 516 B, `File downloaded successfully`, `Submitting leave request… Transitioning to dfuMANIFEST` |
| 4 | Verify application | `ioreg -p IOUSB` | keyboard enumerates as VID `0x3434` / PID `0x0AA0`, product string `Keychron K10 Max`; `0483:df11` gone. **The application is running.** |

So the board is out of the bootloader and running the Prospector build. The
"firmware is corrupt" condition that the earlier session could not clear is
gone — it was the ROM bootloader's stale error flag, and a valid image replaced
it.

**Not yet verified over the wire.** Reading the emitted status frames needs the
raw-HID interface (usage page `0xFF60`), and on this machine that interface is
held by another client:

```console
$ python3 tools/prospector_receiver.py
could not open raw-HID interface: hid_open_path: failed to open IOHIDDevice
from mach entry: (0xE00002C5) exclusive access and device already open
```

Other interfaces of the same device open fine (`usage_page 0x0001/0x000C`), so
this is specific to the vendor-defined collection, not a general HID problem.
`Karabiner-Elements` is running and is the most likely holder; if quitting it
does not help, the host application needs **Input Monitoring** permission
(System Settings → Privacy & Security → Input Monitoring).

Once the interface is free:

```bash
pip install hid
python3 tools/prospector_receiver.py
```


---

## 9. On-hardware verification (final)

Three builds were written to the unit. The first two were **defective**; both
defects were found by actually trying to read frames back, not by reading code.

| Attempt | What it did | Outcome |
|---|---|---|
| 1 | group byte `0xAD`, wireless-preferring routing | firmware ran, but **zero** frames on the wire |
| 2 | group byte `0xAE` | not reached — keyboard was unplugged mid-session |
| 3 | `0xAE` + USB-preferring routing | **frames captured** |

### Defect 1 — group byte collided with a shipped channel

`0xAD` is already used by the `codex_threads` keymap on this board
(`0xAD 0x01 <slot 1..12>`), consumed by
`qmk-vibe-bridge/tools/k10-codex-controller`. Two unrelated producers would have
shared one namespace. Moved to `0xAE`. Fixed in `a3cdad9`.

### Defect 2 — frames were routed to a link that silently drops them

`TRANSPORT_WIRELESS` is `(TRANSPORT_BLUETOOTH | TRANSPORT_P2P4)`. On a keyboard
that is **plugged in but switched to Bluetooth or 2.4 GHz**,
`get_transport()` reports wireless, so the old code took the wireless branch of
`kc_raw_hid_send()`:

```c
else if (wireless_get_state() == WT_CONNECTED) { ... }   /* no else: silent drop */
```

With `KEEP_USB_CONNECTION_IN_WIRELESS_MODE` the USB HID link stays alive the
whole time, so USB was the correct carrier all along. This is also why
`codex_threads` works: it calls plain `raw_hid_send()`, which is USB-only.
Fixed in `21a59fc`.

### The captured frame

```console
raw: ff ff ab cd 22 64 00 18 00 0c 00 00 00 00 00 4c 30 00 00 57 00 2b 00 00 00 00

[Prospector] ver=2.2.3 bat=100% layer=0 prof=0 conn=0 flags=0x0C
             role=standalone periph=[0, 0, 0] mods=0x00 wpm=0 ch=0
             name='L0' id=57002B00 locks=--- valid=yes
```

| Bytes | Field | Value | Check |
|---|---|---|---|
| 0-1 | `manufacturer_id` | `FF FF` | matches the scanner gate |
| 2-3 | `service_uuid` | `AB CD` | matches the scanner gate |
| 4 | `version` | `0x22` | 2.2 |
| 5 | `battery_level` | `0x64` = 100 | real gauge reading |
| 6 | `active_layer` | 0 | real QMK layer |
| 7 | `profile_slot` | `0x18` | dev=0, patch=3, profile=0 |
| 9 | `status_flags` | `0x0C` | USB_CONNECTED \| USB_HID_READY — correct for a wired link |
| 15-18 | `layer_name` | `4C 30 00 00` = `"L0\0\0"` | zero-padded, as the reference specifies |
| 19-22 | `keyboard_id` | `57 00 2B 00` | STM32 UID |
| — | scanner gate | `valid=yes` | **accepted** |

### What this does and does not prove

**Proven on hardware:** the state collector reads real QMK state; the encoder
produces the exact 26-byte Prospector payload; the scanner's own acceptance
predicate accepts it; the carrier delivers it intact; the official keymap and all
normal keyboard function are unaffected (VIA still answers, the firmware version
string reports the new build).

**Still not possible:** the stock **Prospector scanner** will not react, because
it only observes BLE advertisements and this keyboard cannot emit one. The frame
is correct and the radio is the missing piece — see §6.3.

### Debugging note for the controller

`k10-codex-controller`'s `enterBootloader()` writes `0x0B`, but this firmware
does **not** implement VIA's `id_bootloader_jump` — `quantum/via.c` has no such
case, and the `bootloader_jump()` call in `keyboards/keychron/common/factory_test.c`
is commented out. That subcommand is a no-op; entering DFU requires Bootmagic
(hold **Esc** at plug-in), which is enabled and whose key is matrix `(0,0)` = Esc.


### Inspecting frames while the controller runs

`k10-codex-controller` opens the same raw-HID interface, so the two cannot read
at once — the receiver will report `exclusive access` while it is up. To look at
frames, stop the controller for the duration:

```bash
launchctl bootout   gui/501/com.keychron.k10-codex-controller
python3 tools/prospector_receiver.py
launchctl bootstrap gui/501 ~/Library/LaunchAgents/com.keychron.k10-codex-controller.plist
```

This is not a defect in either tool; it is how a single HID collection works.
Both coexist fine otherwise: the controller's validators match on its own command
bytes (`0x01`, `0x04`, `0x07`, `0xA8`, ...), so Prospector's `0xAE` frames are
skipped harmlessly, and Prospector does not use `0xAD`.
