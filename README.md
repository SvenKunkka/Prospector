# K10 Max → Prospector

Making a **Keychron K10 Max** drive a **Prospector** status display, and the
source-level answer to whether that is possible on stock hardware.

## Read this first

**Prospector listens only to BLE advertising**, and it only accepts a 26-byte
*Manufacturer Specific Data* element beginning `FF FF AB CD`. The K10 Max
cannot emit that element: its radio is the **LKBT51 module**, a separate SoC on
SPI that owns the whole Bluetooth stack including GAP advertising, and its
command set contains no way to set advertising data. QMK's STM32F401 is not a
radio.

That conclusion is **case C** of the task. It is not a stopping point:

- the full keyboard-side implementation is written, **builds**, and is staged
  here as a flashable binary;
- a transport that **works today with no extra parts** lets you verify the live
  state collection and the exact protocol frame on real hardware;
- a **compile-verified UART transport** reduces the recommended fix (one small
  BLE radio) to a wiring job.

Full reasoning and evidence: **[`docs/PROSPECTOR.md`](docs/PROSPECTOR.md)**.

## Contents

| Path | What it is |
|---|---|
| `docs/PROSPECTOR.md` | the technical conclusion: protocol, K10 Max architecture, the blocker, and every unblock path ranked |
| `docs/FLASHING.md` | build command, output paths, bootloader entry, flashing, recovery |
| `patches/k10max-prospector.patch` | the complete source change (13 files, +931) |
| `firmware/` | built artifacts + SHA-256: Prospector build and a stock rollback image |
| `tests/run.sh`, `tests/prospector_conformance.c` | host conformance test against the **real ZMK reference struct** |
| `tests/zmk/` | the reference header, verbatim from the pinned revision, for that test |
| `tools/prospector_receiver.py` | decodes the live status frame over Keychron raw HID |

## Build and flash

```bash
export QMK_HOME=$HOME/qmk_firmware          # Keychron fork, branch 2025q3
cd "$QMK_HOME"

qmk compile -kb keychron/k10_max/ansi/rgb -km keychron
qmk flash   -kb keychron/k10_max/ansi/rgb -km keychron
```

Bootloader is the STM32 factory ROM DFU (`0483:DF11`, confirmed from
`info.json`): unplug, hold **Esc**, plug in, confirm with `dfu-util -l`.
Details, including recovery, in [`docs/FLASHING.md`](docs/FLASHING.md).

## Verify without flashing anything

```bash
tests/run.sh                 # 54 checks: layout, flag bits, gate, sentinels
```

The test does not trust this project's header — it compiles the reference
`struct zmk_status_adv_data` beside ours and asserts equal size, all 16 field
offsets, every flag bit, and that the scanner's own acceptance gate accepts the
frame. It is what found a real bug: the layer-name field was being space-padded
where the reference zero-pads.

## Verify on the keyboard with no extra hardware

```bash
pip install hid
python3 tools/prospector_receiver.py
```

```
[Prospector] ver=2.2.3 bat=88% layer=2 prof=0 conn=1 flags=0x30 role=standalone
             periph=[0, 0, 0] mods=0x00 wpm=42 ch=0 name='L2' id=DEADBEEF locks=C-S valid=yes
```

This proves the state collection and the encoding on hardware. It does **not**
make the stock Prospector scanner react — only a BLE advertisement would.

## Honest status

| | |
|---|---|
| Source analysis | done, pinned and cited |
| Firmware written | done — encoder, state, scheduler, 3 transports |
| Builds | 4 configurations verified (rawhid / none / uart / stock) |
| Host conformance | passing, against the real reference struct |
| Flashed to the keyboard | **done** — written, `:leave` accepted, enumerates as `3434:0AA0` and runs |
| Factory image backed up first | **done** — 256 KB read, SHA `ff042683…`, staged in `firmware/` |
| Status frame read back over raw HID | **done on hardware** — 26 bytes captured, scanner gate says `valid=yes` |
| Real state verified | **done** — battery 100 % from the fuel gauge, layer 0, USB flags `0x0C`, STM32 UID |
| Two defects found by testing, both fixed | `0xAD` collision (`a3cdad9`); wireless routing dropped frames (`21a59fc`) |
| Radio-level verification | **not done** — no BLE advertisement was observed, and none can be |
| Stock behaviour preserved | module compiled out when the flag is unset; `keymap.c` untouched |

Nothing was written to the keyboard or to its LKBT51 module by this work.

## What is not in this repository

Two firmware images used by this work are **not** distributed here, because they are
Keychron's rather than this work's. Their SHA-256 are recorded so that an image you
obtain yourself — by dumping your own board before flashing, or from the vendor's own
stock firmware — can be checked against the ones this work used:

| Image | SHA-256 | What it is |
|---|---|---|
| `firmware/k10max_FACTORY_original_256k.bin` | `ff0426833ede5dead8db3f1f2752a581fed73950dc292f55447fc8072fd14601` | a full dump of this board's factory flash, taken before anything was written |
| `firmware/k10max_STOCK_rollback.bin` | `fa59a291589b4a83713779b920bfb701d7dd43f7c984365c80e56ed25716dee7` | the vendor stock image a flash is rolled back to |

`firmware/SHA256SUMS.txt` lists only the images that are present here. The two above
stay in the working tree on the machine that did the work, so a flash can still be
undone; they are simply not published.
