/* Copyright 2026 Keychron
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host conformance test for the Prospector status encoder.
 *
 * The point of this test is that it does NOT trust our own header. It includes
 * the reference `struct zmk_status_adv_data` verbatim (tests/zmk/, copied from
 * the pinned t-ogura/prospector-zmk-module revision) and asserts that:
 *
 *   1. our frame is the same length,
 *   2. every field sits at the same offset,
 *   3. the scanner's acceptance gate accepts our frame,
 *   4. unavailable values use the protocol's own sentinels instead of a
 *      plausible-looking invention.
 *
 * Build/run: tests/run.sh
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "prospector_status.h"
#include "zmk/status_advertisement.h" /* the reference, not ours */

static int failures;
static int checks;

#define CHECK(cond, ...)                            \
    do {                                            \
        checks++;                                   \
        if (cond) {                                 \
            printf("  [PASS] " __VA_ARGS__);        \
        } else {                                    \
            failures++;                             \
            printf("  [FAIL] " __VA_ARGS__);        \
        }                                           \
        printf("\n");                               \
    } while (0)

#define CHECK_EQ(a, b, label) CHECK((long)(a) == (long)(b), "%s (%ld == %ld)", label, (long)(a), (long)(b))

/* --- 3. the scanner's gate, transcribed from status_scanner.c ------------- */
static int scanner_accepts(const uint8_t *ad_value, size_t len) {
    /* ad_value points at the manufacturer-data body (company id first). */
    if (len < sizeof(struct zmk_status_adv_data)) return 0;
    const struct zmk_status_adv_data *d = (const struct zmk_status_adv_data *)ad_value;
    return d->manufacturer_id[0] == 0xFF && d->manufacturer_id[1] == 0xFF && d->service_uuid[0] == 0xAB && d->service_uuid[1] == 0xCD;
}

int main(void) {
    printf("Prospector encoder conformance test\n");
    printf("reference: t-ogura/prospector-zmk-module v2.2.3 (tests/zmk/)\n\n");

    /* --- 1 & 2: layout must match the reference exactly ------------------ */
    printf("layout vs. reference struct\n");
    CHECK_EQ(sizeof(struct prospector_adv_data), sizeof(struct zmk_status_adv_data), "payload size");
    CHECK_EQ(sizeof(struct prospector_adv_data), 26, "payload is 26 bytes");

    CHECK_EQ(offsetof(struct prospector_adv_data, manufacturer_id), offsetof(struct zmk_status_adv_data, manufacturer_id), "offset manufacturer_id");
    CHECK_EQ(offsetof(struct prospector_adv_data, service_uuid), offsetof(struct zmk_status_adv_data, service_uuid), "offset service_uuid");
    CHECK_EQ(offsetof(struct prospector_adv_data, version), offsetof(struct zmk_status_adv_data, version), "offset version");
    CHECK_EQ(offsetof(struct prospector_adv_data, battery_level), offsetof(struct zmk_status_adv_data, battery_level), "offset battery_level");
    CHECK_EQ(offsetof(struct prospector_adv_data, active_layer), offsetof(struct zmk_status_adv_data, active_layer), "offset active_layer");
    CHECK_EQ(offsetof(struct prospector_adv_data, profile_slot), offsetof(struct zmk_status_adv_data, profile_slot), "offset profile_slot");
    CHECK_EQ(offsetof(struct prospector_adv_data, connection_count), offsetof(struct zmk_status_adv_data, connection_count), "offset connection_count");
    CHECK_EQ(offsetof(struct prospector_adv_data, status_flags), offsetof(struct zmk_status_adv_data, status_flags), "offset status_flags");
    CHECK_EQ(offsetof(struct prospector_adv_data, device_role), offsetof(struct zmk_status_adv_data, device_role), "offset device_role");
    CHECK_EQ(offsetof(struct prospector_adv_data, device_index), offsetof(struct zmk_status_adv_data, device_index), "offset device_index");
    CHECK_EQ(offsetof(struct prospector_adv_data, peripheral_battery), offsetof(struct zmk_status_adv_data, peripheral_battery), "offset peripheral_battery");
    CHECK_EQ(offsetof(struct prospector_adv_data, layer_name), offsetof(struct zmk_status_adv_data, layer_name), "offset layer_name");
    CHECK_EQ(offsetof(struct prospector_adv_data, keyboard_id), offsetof(struct zmk_status_adv_data, keyboard_id), "offset keyboard_id");
    CHECK_EQ(offsetof(struct prospector_adv_data, modifier_flags), offsetof(struct zmk_status_adv_data, modifier_flags), "offset modifier_flags");
    CHECK_EQ(offsetof(struct prospector_adv_data, wpm_value), offsetof(struct zmk_status_adv_data, wpm_value), "offset wpm_value");
    CHECK_EQ(offsetof(struct prospector_adv_data, channel), offsetof(struct zmk_status_adv_data, channel), "offset channel");

    /* --- status flag bits must agree ------------------------------------- */
    printf("\nflag bit agreement\n");
    CHECK_EQ(PROSPECTOR_FLAG_CAPS_WORD, ZMK_STATUS_FLAG_CAPS_WORD, "CAPS_WORD bit");
    CHECK_EQ(PROSPECTOR_FLAG_CHARGING, ZMK_STATUS_FLAG_CHARGING, "CHARGING bit");
    CHECK_EQ(PROSPECTOR_FLAG_USB_CONNECTED, ZMK_STATUS_FLAG_USB_CONNECTED, "USB_CONNECTED bit");
    CHECK_EQ(PROSPECTOR_FLAG_USB_HID_READY, ZMK_STATUS_FLAG_USB_HID_READY, "USB_HID_READY bit");
    CHECK_EQ(PROSPECTOR_FLAG_BLE_CONNECTED, ZMK_STATUS_FLAG_BLE_CONNECTED, "BLE_CONNECTED bit");
    CHECK_EQ(PROSPECTOR_FLAG_BLE_BONDED, ZMK_STATUS_FLAG_BLE_BONDED, "BLE_BONDED bit");
    CHECK_EQ(PROSPECTOR_MOD_LCTL, ZMK_MOD_FLAG_LCTL, "LCTL bit");
    CHECK_EQ(PROSPECTOR_MOD_RGUI, ZMK_MOD_FLAG_RGUI, "RGUI bit");

    /* --- 3: the task's MVP frame ----------------------------------------- */
    printf("\nMVP frame: battery 88, layer 2, caps lock on\n");
    struct prospector_state st;
    memset(&st, 0, sizeof(st));
    st.battery_level = 88;
    st.battery_known = true;
    st.active_layer  = 2;
    st.status_flags  = PROSPECTOR_FLAG_BLE_CONNECTED | PROSPECTOR_FLAG_BLE_BONDED;
    st.profile_slot  = 0;
    st.channel       = 0;
    uint8_t id[4]    = {0xDE, 0xAD, 0xBE, 0xEF};
    memcpy(st.keyboard_id, id, 4);

    struct prospector_adv_data adv;
    size_t                     n = prospector_encode(&st, &adv);
    const uint8_t             *b = (const uint8_t *)&adv;

    CHECK_EQ(n, 26, "encoder returned 26 bytes");
    CHECK_EQ(b[0], 0xFF, "byte 0 company id hi");
    CHECK_EQ(b[1], 0xFF, "byte 1 company id lo");
    CHECK_EQ(b[2], 0xAB, "byte 2 service uuid hi");
    CHECK_EQ(b[3], 0xCD, "byte 3 service uuid lo");
    CHECK_EQ(b[4], 0x22, "byte 4 version 2.2");
    CHECK_EQ(b[5], 88, "byte 5 battery 88");
    CHECK_EQ(b[6], 2, "byte 6 layer 2");
    CHECK_EQ(b[7] & 0x07, 0, "byte 7 profile 0");
    CHECK_EQ((b[7] >> 3) & 0x07, 3, "byte 7 patch 3");
    CHECK_EQ(b[10], 0, "byte 10 role standalone");
    CHECK_EQ(b[23], 0, "byte 23 modifiers clear");
    CHECK_EQ(b[24], 0, "byte 24 wpm unknown -> 0");
    CHECK_EQ(b[25], 0, "byte 25 channel all");

    CHECK(scanner_accepts(b, 26), "scanner acceptance gate accepts the frame");

    /* The full AD element the zero-length byte budget must fit in. */
    CHECK_EQ(PROSPECTOR_ADV_SIZE + 2 + 3, 31, "AD element + Flags == 31-byte legacy PDU");

    /* --- 4: sentinels, not invented numbers ------------------------------ */
    printf("\nunknown values use protocol sentinels\n");
    struct prospector_state unk;
    memset(&unk, 0, sizeof(unk));
    unk.active_layer  = 5;
    unk.battery_known = false;
    struct prospector_adv_data unkadv;
    prospector_encode(&unk, &unkadv);
    const uint8_t *ub = (const uint8_t *)&unkadv;
    CHECK_EQ(ub[5], 0, "unknown battery -> 0, not a guess");
    CHECK_EQ(ub[12], 0, "peripheral battery N/A -> 0");
    CHECK_EQ(ub[13], 0, "peripheral battery N/A -> 0");
    CHECK_EQ(ub[14], 0, "peripheral battery N/A -> 0");
    CHECK_EQ(ub[15], 'L', "layer label falls back to L<n>");
    CHECK_EQ(ub[16], '5', "layer label index is truthful");

    /* --- layer label ------------------------------------------------------ */
    printf("\nlayer label handling\n");
    struct prospector_state named;
    memset(&named, 0, sizeof(named));
    named.active_layer = 3;
    named.layer_name   = "GAMING";
    struct prospector_adv_data nadv;
    prospector_encode(&named, &nadv);
    /* The field is 4 bytes, NOT NUL-terminated: the first 4 chars are used. */
    CHECK_EQ(memcmp(nadv.layer_name, "GAMI", 4), 0, "long label truncated to 4 chars");

    /* A short label must be zero-padded, not space-padded: the reference does
     * memset(0)+memcpy, and the scanner display would otherwise show a trailing
     * space. */
    struct prospector_state shortname;
    memset(&shortname, 0, sizeof(shortname));
    shortname.active_layer = 1;
    shortname.layer_name   = "AB";
    struct prospector_adv_data sadv;
    prospector_encode(&shortname, &sadv);
    CHECK_EQ(sadv.layer_name[0], 'A', "short label byte 0");
    CHECK_EQ(sadv.layer_name[1], 'B', "short label byte 1");
    CHECK_EQ(sadv.layer_name[2], 0, "short label zero-padded at byte 2");
    CHECK_EQ(sadv.layer_name[3], 0, "short label zero-padded at byte 3");

    /* Fallback label for layers > 9 keeps all 4 bytes usable. */
    struct prospector_state l15;
    memset(&l15, 0, sizeof(l15));
    l15.active_layer = 15;
    struct prospector_adv_data l15adv;
    prospector_encode(&l15, &l15adv);
    CHECK_EQ(memcmp(l15adv.layer_name, "L15", 3), 0, "layer 15 fallback label");

    /* --- host dump helper ------------------------------------------------ */
    char dump[160];
    prospector_format(&adv, dump, sizeof(dump));
    printf("\nframe dump: %s\n", dump);

    printf("\n%s (%d checks, %d failures)\n", failures ? "FAILED" : "ALL CHECKS PASSED", checks, failures);
    return failures ? 1 : 0;
}
