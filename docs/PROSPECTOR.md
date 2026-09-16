# Keychron K10 Max → Prospector: source-level findings and firmware

Target: `keychron/k10_max/ansi/rgb:keychron` on Keychron's QMK fork, branch
`2025q3` (`eb4afcb`), USB `3434:0AA0`.

Everything below is read from source at pinned revisions. Where something could
not be established from source, it says so instead of guessing.

| Component | Pinned revision |
|---|---|
| Prospector Scanner / protocol of record | `t-ogura/prospector-zmk-module` `v2.2.3` = `72a71ae` |
| Prospector hardware | `carrefinho/prospector` (XIAO nRF52840 + 1.69" touch LCD) |
| Keyboard firmware | `Keychron/qmk_firmware` `2025q3` = `eb4afcb` |

---

## 1. Technical conclusion

**Prospector does not connect to the keyboard.** It passively scans BLE
advertisements and matches a 26-byte **Manufacturer Specific Data** element
(`BT_DATA_MANUFACTURER_DATA`, AD type `0xFF`) whose first four bytes are
`FF FF AB CD`. There is no GATT ingest path, no USB path and no dongle path on
the scanner side.

**The K10 Max cannot produce that advertisement, and the limit is not in QMK.**
The radio belongs to the LKBT51 module — a separate SoC on SPI that owns the
entire Bluetooth stack including GAP advertising. The STM32F401 that runs QMK is
not a radio and has no BLE stack at all. The module's command interface has no
advertising-data command, and its own firmware cannot be replaced with the
material available (encrypted image, token-gated DFU).

So the honest answer to *"can the current QMK main MCU control the wireless
module to send the BLE advertising data Prospector needs?"* is **no**, and that
is **case C**.

What this deliverable therefore provides is:

1. the complete keyboard-side implementation, built and verified;
2. a transport that **works today** over a channel the keyboard already has, so
   the state collection and the encoder can be verified on real hardware with no
   extra parts and no module changes;
3. a **compile-verified** UART transport that turns the recommended fix (one
   external BLE radio) into a wiring job rather than a firmware project.

---

## 2. How Prospector discovers a keyboard (source-verified)

`src/status_scanner.c`, `scan_callback()`, in the advertise-report handler:

```c
if (ad_type == BT_DATA_MANUFACTURER_DATA) {
    if (len >= sizeof(struct zmk_status_adv_data)) {          /* >= 26 */
        const struct zmk_status_adv_data *data = ...;
        if (data->manufacturer_id[0] == 0xFF && data->manufacturer_id[1] == 0xFF &&
            data->service_uuid[0] == 0xAB && data->service_uuid[1] == 0xCD) {
            /* accepted */
        } else {
            LOG_DBG("Non-Prospector device: %02X%02X %02X%02X", ...);
        }
    } else {
        LOG_DBG("Manufacturer data too short: %d bytes", len);
    }
}
```

Facts that follow directly from it:

- Discovery is **BLE advertising only**. The scanner parses every AD structure in
  the advertising / scan-response payload; it never connects for status.
- The device **name** is read separately from an AD structure of type `0x08` /
  `0x09` and is only used for display. A name alone registers nothing.
- The **channel filter** runs after the identity gate; keyboard `channel = 0`
  is accepted by any scanner.
- Scanning is `BT_LE_SCAN_TYPE_ACTIVE`, so the scanner can also pull the device
  name from a scan response.

### 2.1 The 26-byte frame

`include/zmk/status_advertisement.h`, `struct zmk_status_adv_data`:

| Off | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | `manufacturer_id` | `FF FF` — "reserved/local use", **not** a SIG company id |
| 2 | 2 | `service_uuid` | `AB CD` — Prospector's own discriminator |
| 4 | 1 | `version` | `[7:4]` major, `[3:0]` minor. v2.2.3 → `0x22` |
| 5 | 1 | `battery_level` | 0-100 (central/standalone) |
| 6 | 1 | `active_layer` | 0-15 |
| 7 | 1 | `profile_slot` | `[6]` dev, `[5:3]` patch, `[2:0]` profile |
| 8 | 1 | `connection_count` | 0-5 |
| 9 | 1 | `status_flags` | see below |
| 10 | 1 | `device_role` | 0 standalone / 1 central / 2 peripheral |
| 11 | 1 | `device_index` | split index |
| 12 | 3 | `peripheral_battery[3]` | 0 = N/A |
| 15 | 4 | `layer_name` | **4 bytes, NOT NUL-terminated**, zero-padded |
| 19 | 4 | `keyboard_id` | hardware-unique (HWINFO on nRF) |
| 23 | 1 | `modifier_flags` | LCTL/LSFT/LALT/LGUI/RCTL/RSFT/RALT/RGUI |
| 24 | 1 | `wpm_value` | 0 = inactive/unknown |
| 25 | 1 | `channel` | 0 = accept all |

The payload is the *body* of one AD structure, so on air it is
`[0x1B][0xFF][26 bytes]` = 28 bytes. With the 3-byte Flags AD that is exactly
the 31-byte legacy advertising budget — which is why a conforming sender puts
the **name in the scan response**, not in the advertising payload.

### 2.2 What the scanner actually consumes

`scanner_core.c` reads only three status bits:

```c
pending_data.usb_ready     = (d->status_flags & ZMK_STATUS_FLAG_USB_HID_READY) != 0;
pending_data.ble_connected = (d->status_flags & ZMK_STATUS_FLAG_BLE_CONNECTED) != 0;
pending_data.ble_bonded    = (d->status_flags & ZMK_STATUS_FLAG_BLE_BONDED) != 0;
```

### 2.3 Fields the protocol does **not** have

**Caps Lock, Num Lock and Scroll Lock have no field in the Prospector v2.2.3
wire format.** The only related flag is `ZMK_STATUS_FLAG_CAPS_WORD` (ZMK's
*caps-word* feature, not the Caps Lock LED) and no scanner widget reads it.
They have deliberately **not** been invented into the frame. Instead they ride
in the spare byte of the raw-HID verification carrier (see §5), where they cost
nothing and cannot corrupt protocol compatibility.

Similarly, layer *names* require a layer-name table, which QMK does not have on
this board; the encoder emits the truthful `L<n>` fallback, exactly as the
reference does for its own peripheral case.

---

## 3. K10 Max wireless architecture (source-verified)

```
STM32F401 (QMK / ChibiOS)                 LKBT51 module (separate SoC)
  matrix scan, keymap, layers, RGB   SPI1   Bluetooth + 2.4 GHz stack
  USB HID, battery gauge        <-------->  HID-over-BLE, bonding,
  advertising: NONE                         profiles, GAP ADVERTISING
```

Evidence:

- `keyboards/keychron/k10_max/info.json`: `"processor": "STM32F401"`,
  `"bootloader": "stm32-dfu"`, `"usb": {"vid": "0x3434"}`.
- `keyboards/keychron/k10_max/config.h`: `LKBT51_RESET_PIN C4`,
  `WIRELESS_TO_MCU_INT_PIN B1`, `MCU_TO_WIRELESS_INT_PIN A4`,
  `BT_MODE_SELECT_PIN A10`, `P24G_MODE_SELECT_PIN A9`.
- `keyboards/keychron/common/wireless/lkbt51.h`: `#define WT_DRIVER SPID1`
  — the module is driven over **SPI**, framed `84 7E 00 00 AA 56 <len> <~len>
  <sn> <payload> <cksum>` (see `lkbt51_send_cmd()`).
- `keyboards/keychron/common/wireless/wireless.mk` compiles `lkbt51.c` for
  this board.

### 3.1 The complete module command set

From the enum in `lkbt51.c`:

| Range | Commands |
|---|---|
| `0x11`-`0x17` | keyboard, NKRO, consumer, system, FN, mouse, boot-KB reports |
| `0x21`-`0x25` | `PAIRING`, `CONNECT`, `DISCONNECT`, `SWITCH_HOST`, `READ_STATE_REG` |
| `0x31`-`0x33` | `BATTERY_MANAGE`, `UPDATE_BAT_LVL`, `UPDATE_BAT_STATE` |
| `0x40`-`0x4A` | `GET_MODULE_INFO`, `SET/GET_CONFIG`, `SET/GET_BDA`, `SET/GET_NAME`, `WRTE_CSTM_DATA`, `SET_MS_SWIFT_PAIR_NAME` |
| `0x60`-`0x65` | DFU: `GET_DFU_VER`, `HAND_SHAKE_TOKEN`, `START_DFU`, `SEND_FW_DATA`, `VERIFY_CRC32`, `SWITCH_FW` |
| `0x71`-`0x73` | `FACTORY_RESET`, `IO_TEST`, `RADIO_TEST` |
| `0x91`-`0x93` | `RAW_HID_INIT`, `RAW_HID_RX`, `RAW_HID_TX` |

**There is no command that sets advertising data, scan-response data, or
manufacturer-specific data.** The only GAP-visible things QMK can influence are
the device name (`SET_NAME 0x45`) and the module's vendor/product id
(`SET_CONFIG 0x41`, whose struct carries no advertising field —
`event_mode`, timeouts, `pairing_mode`, `report_rate`, `vendor_id_source`,
`vendor_id`, `product_id`, three connection-interval values, two reserved
bytes). Changing the name is not the Prospector protocol and the scanner would
still reject the advertisement, because it matches on manufacturer data only.

Verification that nothing else exists:

```console
$ grep -rniE 'advertis|manufacturer_data|bt_le_adv|scan_rsp' keyboards/keychron/
(no matches in the stock tree)
```

An interesting but useless near-miss: `SET_MS_SWIFT_PAIR_NAME` proves the
module *can* attach a manufacturer-specific AD element (Microsoft Swift Pair),
but it is company id `0x0006` with a Microsoft-defined body — the scanner would
log it as `Non-Prospector device: 0006...`. There is no command to make it carry
`FF FF AB CD`.

### 3.2 Two candidate escape hatches, both checked and closed

- **Raw-HID passthrough to the module** (`lkbt51_dfu_rx`). The host can push
  frames that QMK relays to the module, but they are filtered:
  ```c
  if ((payload[0] & 0xF0) == 0x60) {   /* DFU command range only */
      lkbt51_send_cmd(payload, payload_len - 2, data[1] == 0x56, retry);
  }
  ```
  Only `0x60`-`0x6F` reach the module, and only the vendor DFU tool drives it.
- **`WRTE_CSTM_DATA` (`0x49`)** is declared and implemented in `lkbt51.c` but
  **called from nowhere** in `keyboards/`. Its SPI window (`84 7A ...`, magic
  `0x9527`) is undocumented. Nothing indicates it reaches the advertising
  payload. Without module documentation this cannot be claimed either way — it
  is recorded here as an *open, unverified* hypothesis, not a solution.

### 3.3 Why the module firmware cannot simply be patched

| Requirement | Status |
|---|---|
| Module source | not published (the public repo has only the STM32-side SPI driver) |
| SoC / SDK / datasheet | not published |
| A firmware image to patch | vendor `.kfw` only; the copy available locally is high-entropy (Shannon ≈ 7.998 bits/byte) — encrypted and/or compressed, no vector table, no readable strings |
| Write access | DFU is gated by the module-internal `HAND_SHAKE_TOKEN` (`0x61`) before `START_DFU`/`VERIFY_CRC32`; QMK only relays opaque frames |

So **modifying the wireless module is not achievable with the material at
hand**, and that is the option ranked above adding hardware.

---

## 4. What was built

Layering, with each layer knowing only the one below it:

```
QMK state                     layer_state, mods, battery, transport, LED state
    |
prospector_qmk.c              collect state + broadcast policy
    |
prospector_status.c           pure encoder -> 26-byte frame
    |
prospector_transport.c        the only layer that touches a wire
```

Files added under `keyboards/keychron/common/prospector/`:

| File | Role |
|---|---|
| `prospector_status.h/.c` | pure, hardware-free encoder; `_Static_assert` pins the frame at 26 bytes |
| `prospector_qmk.h/.c` | state collection + scheduler; the reference broadcast policy |
| `prospector_transport.h/.c` | transport backends: `none` / `rawhid` / `uart` |
| `prospector.mk` | build integration, gated on `PROSPECTOR_ENABLE` |

Modified:

| File | Change |
|---|---|
| `k10_max/k10_max.c` | two guarded calls (`prospector_status_init`, `prospector_status_task`) in the existing keyboard-level hooks |
| `k10_max/ansi/rgb/keymaps/keychron/rules.mk` | opt in: `PROSPECTOR_ENABLE = yes` |
| `common/wireless/wireless.c/.h` | additive read-only accessor for the active Bluetooth host slot (it is otherwise file-static) |
| `k10_max/halconf.h`, `k10_max/mcuconf.h` | USART6 enabled **only** when `PROSPECTOR_TRANSPORT=uart` is selected |

No keymap copy is needed: the module hooks into the keyboard-level
`keyboard_post_init_kb()` / `keychron_task_kb()`, so `keymap.c` is untouched.
The only change to the keymap directory is an opt-in block in its `rules.mk`
(`PROSPECTOR_ENABLE`, the transport choice, and `WPM_ENABLE` /
`CAPS_WORD_ENABLE` — the latter two are what supply real WPM and caps-word
values to the frame; without them those fields report the protocol's "unknown"
sentinel). Delete that block and the keymap builds stock.

### 4.1 How the main loop is protected

- Everything is driven from the existing periodic task hook; nothing is added to
  the matrix scan path.
- No `wait_ms()`, no blocking SPI, no busy-wait. Each step is a timer
  comparison plus one non-blocking transport call.
- Idle detection uses QMK's own `last_input_activity_elapsed()`, so no keypress
  hook is needed.
- Broadcast cadence follows the reference: 5×15 ms burst on boot and on layer /
  modifier / profile change, 1 Hz while typing, 30 s idle. That is well under any
  advertising-load concern.
- Compiled out entirely when `PROSPECTOR_ENABLE` is unset.

### 4.2 The three transports

| `PROSPECTOR_TRANSPORT` | What it does | Verified |
|---|---|---|
| `none` (default) | counts frames, sends nothing | builds; frames counted |
| `rawhid` | sends the frame over Keychron's raw-HID channel — **works over USB and Bluetooth** | builds |
| `uart` | writes `[AA][55][0x1B][0xFF][26 bytes][XOR][00]` to a ChibiOS serial driver | builds (USART6) |

The raw-HID carrier is:

```
[0]      0xAE   Prospector group
[1]      0x01   sub-command: status frame
[2]      26     payload length
[3..28]  the 26-byte Prospector payload, verbatim
[29]     host lock state (bit0 Caps, bit1 Num, bit2 Scroll) - NOT protocol
[30..31] zero
```

Because the payload is copied verbatim starting at byte 3, a receiver can lift
`report[3:29]` straight into an advertisement body unchanged.

**On the choice of `0xAE`.** The raw-HID command space on this board is already
occupied:

| Byte | Owner |
|---|---|
| `0x01`-`0x0B` | VIA core (get/set keycode, dynamic keymap, bootloader jump) |
| `0xA0`-`0xA3` | Keychron (protocol version, firmware version, features, default layer) |
| `0xA7`-`0xAB` | Keychron (misc, RGB, analog matrix, wireless DFU, factory test) |
| `0xAC` | local experiment (`claude_rgb`) |
| `0xAD` | **`codex_threads` "focus slot": `0xAD 0x01 <slot 1..12>`** |

`0xAD` is a *shipped* keyboard-to-host channel on this keyboard, consumed by
`qmk-vibe-bridge/tools/k10-codex-controller`:

```js
if (response?.[0] === 0xad && response?.[1] === 0x01 && response?.[2] >= 1 && response?.[2] <= 12)
```

An earlier revision of this module used `0xAD` as well. That was a mistake: two
unrelated producers would have shared one namespace, and every Prospector frame
would have entered the slot-controller's read loop. `0xAE` is the next genuinely
free byte, and it is what the shipped firmware uses.

---

## 5. Verification performed

```console
$ qmk compile -kb keychron/k10_max/ansi/rgb -km keychron     # Prospector ON
  77228 bytes, build success

$ qmk compile -kb keychron/k10_max/ansi/rgb -km default       # stock, module OFF
  72396 bytes, build success

$ tests/run.sh                                                # host conformance
  ALL CHECKS PASSED (54 checks, 0 failures)
```

The host test does not trust this project's own header: it compiles the
**reference `struct zmk_status_adv_data`** next to ours and asserts equal size,
all 16 field offsets, every flag bit, and that the scanner's own acceptance gate
accepts the produced frame. It also asserts that unmeasured values use the
protocol's sentinels instead of a plausible-looking invention. This is how the
zero-padding bug in the layer-name field was found (the encoder was
space-padding; the reference zero-pads).

What was **not** done: nothing has been written to the keyboard or its module,
and no BLE advertisement has been observed from this firmware. The claim "the
scanner will accept these bytes" rests on the scanner's source, not on a radio
observation.

---

## 6. Unblocking the real goal

Ranked by the stated preference *no new hardware > modify the module > add
hardware*.

### 6.1 No new hardware — the raw-HID path (works today)

This is real and testable now, and it is exactly the "K10 Max → Raw HID →
display" route. What it cannot do is make the stock **Prospector scanner**
recognise the keyboard, because the scanner only reads BLE advertising.

```console
$ pip install hid
$ tools/prospector_receiver.py          # decodes the 26-byte frame live
[Prospector] keychron  ver=2.2.3 bat=88% layer=2 prof=0 conn=1 flags=0x30
            mods=0x00 wpm=42 name=L2 id=DEADBEEF  locks=C--  valid=yes
```

Use it to prove on hardware that layer/battery/modifier tracking is correct
before spending anything on parts.

### 6.2 Modify the module — closed

See §3.2 and §3.3. Encrypted image, token-gated DFU, no published SDK, and no
advertising command to hijack.

### 6.3 Add one small radio — recommended, and now mostly pre-built

```
STM32F401 ──UART (USART6, PA11 TX / PA12 RX)──> nRF52840 ──BLE adv──> Prospector
  (QMK, this firmware)                          (tiny Zephyr/nRF5 app)
```

- The keyboard side is **already written and already builds**: select
  `PROSPECTOR_TRANSPORT = uart` and `PROSPECTOR_UART_DRIVER = SD6`.
- The radio side is ~100 lines: read 32-byte framed packets, forward bytes
  2..29 into the advertisement as `BT_DATA_MANUFACTURER_DATA`, advertise
  non-connectably. The frame is pre-built and self-syncing, so the radio needs no
  Prospector knowledge at all.
- Nothing about the keyboard's own Bluetooth / 2.4 GHz / USB / RGB / remapping
  is touched.

**Caveat, stated plainly:** USART6 on PA11/PA12 was chosen because those pins are
unused by the matrix and by the wireless module in `info.json` and
`config.h`. That is a pin-conflict analysis, **not** a schematic review — pad
availability on the actual PCB still has to be confirmed before relying on it.

### 6.4 The other direction — change the Prospector, not the keyboard

Because Keychron's raw HID is bidirectional over Bluetooth (it is how the
Launcher web app works), a custom Prospector firmware could act as a BLE central:
connect to the keyboard's HID service and poll status over the raw-HID channel
instead of scanning advertisements. This needs no keyboard change and no new
hardware, but it **consumes one of the keyboard's three Bluetooth host slots**
and means the display is no longer a passive scanner. It is a genuine option and
is recorded here for completeness, not implemented.
