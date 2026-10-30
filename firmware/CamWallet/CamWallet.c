#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "pico/stdlib.h"

#include "drivers/camera.h"
#include "drivers/lcd.h"
#include "drivers/touch.h"
#include "qr.h"
#include "ui/scan_preview.h"
#include "ur_transport.h"
#include "cur_memory.h"
#include "evm_request.h"
#include "ui/evm_review.h"

#define QR_ECHO_HOLD_SECONDS 30u
#define UR_ECHO_HOLD_SECONDS 60u
#define UR_FRAME_MS 400u

_Static_assert(CAMERA_WIDTH == LCD_WIDTH && CAMERA_HEIGHT == LCD_HEIGHT,
               "Preview dimensions must match the LCD");

/* DMA32 requires word alignment. Capture and display always own different
 * buffers; no pixel copy or dynamic allocation is needed.
 */
static _Alignas(4) uint8_t camera_frames[2][CAMERA_FRAME_BYTES];
static qr_payload_t received_payload;
static qr_symbol_t response_symbol;
static qr_payload_t last_payload;
static bool have_last_payload;
static uint64_t last_received_at;
static uint64_t next_result_report;
static cw_ur_message_t ur_message;
static evm_request_t evm_request;
static bool touch_ready;

static void show_camera_error(const char *reason)
{
    lcd_clear(LCD_BLACK);
    lcd_draw_text(10, 32, "CAMERA ERROR", 1, LCD_RED, LCD_BLACK);
    lcd_draw_text(10, 80, reason, 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(10, 128, "SEE USB LOG", 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(10, 160, "POWER OFF BEFORE", 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(10, 184, "CHECKING THE CABLE", 1, LCD_WHITE, LCD_BLACK);
}

static const char *scan_hint(qr_scan_status_t status)
{
    switch (status) {
    case QR_SCAN_NONE: return "NO QR FOUND";
    case QR_SCAN_DECODE_FAILED: return "QR DECODE FAILED";
    case QR_SCAN_OK: return "QR DECODED";
    case QR_SCAN_TOO_LONG: return "QR OVER 128 BYTES";
    case QR_SCAN_UNSUPPORTED: return "QR MODE UNSUPPORTED";
    case QR_SCAN_EMPTY: return "QR IS EMPTY";
    case QR_SCAN_BAD_FRAME: return "QR FRAME ERROR";
    default: return "SCAN QR";
    }
}

static void log_scan_info(qr_scan_status_t status, const qr_scan_info_t *info)
{
    printf("[QR] scan: diag=2 candidates=%u decoded=%u gray=%u..%u status=\"%s\" error=\"%s\" mirrored=\"%s\"\n",
           info->candidates, info->decoded, (unsigned)info->gray_min,
           (unsigned)info->gray_max, scan_hint(status),
           info->decode_error != NULL ? info->decode_error : "none",
           info->mirrored_error != NULL ? info->mirrored_error : "none");
}

static void log_payload(const qr_payload_t *payload, const char *label)
{
    static const char hex_digits[] = "0123456789abcdef";
    char hex[QR_PAYLOAD_MAX_BYTES * 2u + 1u];
    char text[QR_PAYLOAD_MAX_BYTES * 4u + 1u];
    size_t text_length = 0;
    for (size_t i = 0; i < payload->length; ++i) {
        const uint8_t byte = payload->bytes[i];
        hex[2u * i] = hex_digits[byte >> 4];
        hex[2u * i + 1u] = hex_digits[byte & 15u];
        if (byte == '\\' || byte == '"') {
            text[text_length++] = '\\';
            text[text_length++] = (char)byte;
        } else if (byte >= 32u && byte <= 126u) {
            text[text_length++] = (char)byte;
        } else {
            text[text_length++] = '\\';
            text[text_length++] = 'x';
            text[text_length++] = hex_digits[byte >> 4];
            text[text_length++] = hex_digits[byte & 15u];
        }
    }
    hex[2u * payload->length] = '\0';
    text[text_length] = '\0';
    /* Send complete lines instead of many tiny USB writes. */
    printf("[QR] %s: bytes=%u eci=%lu\n[QR] payload_hex=%s\n[QR] text=\"%s\"\n",
           label, (unsigned)payload->length, (unsigned long)payload->eci, hex, text);
}

static void report_last_result(void)
{
    const uint64_t now = time_us_64();
    if (!have_last_payload || now < next_result_report) {
        return;
    }
    printf("[QR] last success age=%lus\n",
           (unsigned long)((now - last_received_at) / 1000000u));
    log_payload(&last_payload, "last result");
    next_result_report = now + 5000000u;
}

static void show_echo(uint8_t *frame, const qr_payload_t *payload)
{
    /* The pending capture has been drained before reusing this buffer. */
    if (!qr_render_rgb565_be(&response_symbol, frame, CAMERA_FRAME_BYTES,
                             LCD_WIDTH, LCD_HEIGHT)) {
        printf("[QR] render failed; returning to scanner\n");
        return;
    }
    lcd_display_rgb565_be(frame, CAMERA_FRAME_BYTES);
    lcd_draw_text(10, 8, "QR ECHO / TEST", 1, LCD_BLACK, LCD_WHITE);
    char label[22];
    snprintf(label, sizeof(label), "%u BYTES ECI=%lu", (unsigned)payload->length,
             (unsigned long)payload->eci);
    lcd_draw_text(10, 280, label, 1, LCD_BLACK, LCD_WHITE);
    printf("[QR] echo ready: original payload bytes, hold=%us; RESET also rescans\n",
           QR_ECHO_HOLD_SECONDS);

    const absolute_time_t deadline = make_timeout_time_ms(QR_ECHO_HOLD_SECONDS * 1000u);
    unsigned last_seconds = 0;
    while (!time_reached(deadline)) {
        const int64_t remaining_us = absolute_time_diff_us(get_absolute_time(), deadline);
        const unsigned seconds = remaining_us > 0
            ? (unsigned)((remaining_us + 999999) / 1000000) : 0;
        if (seconds != last_seconds) {
            snprintf(label, sizeof(label), "RESCAN IN %us", seconds);
            lcd_fill_rect(0, 300, LCD_WIDTH, 20, LCD_WHITE);
            lcd_draw_text(10, 302, label, 1, LCD_BLACK, LCD_WHITE);
            last_seconds = seconds;
            report_last_result();
        }
        sleep_ms(20);
    }
    printf("[QR] resuming scanner\n");
}

static const char *prepare_ur_response(bool *evm)
{
    *evm = false;
    if (strcmp(ur_message.type, "bytes") == 0) {
        const uint8_t *bytes;
        size_t size;
        return cw_cbor_unwrap_bytes(ur_message.cbor, ur_message.length, &bytes, &size)
            ? NULL : "UR BYTES INVALID";
    }
    if (strcmp(ur_message.type, "eth-sign-request") != 0) return "UR TYPE UNSUPPORTED";
    const char *error = evm_parse_request(ur_message.cbor, ur_message.length, &evm_request);
    if (error) return error;
    /* Preserve the exact request until the user's decision binds its digest. */
    *evm = true;
    return NULL;
}

static evm_decision_t show_evm_review(uint8_t *frame)
{
    char uuid[33], address[41], value[79], fee[79];
    evm_hex(evm_request.request_id, 16, uuid);
    evm_hex(evm_request.to, 20, address);
    evm_uint256_decimal(evm_request.value, value);
    evm_uint256_decimal(evm_request.max_total_fee, fee);
    printf("[EVM] REVIEW / NOT SIGNED request_id=%s chain_id=%" PRIu64 " nonce=%" PRIu64 "\n",
           uuid, evm_request.chain_id, evm_request.nonce);
    printf("[EVM] to=0x%s value_wei=%s gas_limit=%" PRIu32 "\n", address, value, evm_request.gas_limit);
    printf("[EVM] max_fee_per_gas=%" PRIu64 " priority_fee_per_gas=%" PRIu64 " max_total_fee_wei=%s\n",
           evm_request.max_fee_per_gas, evm_request.priority_fee_per_gas, fee);
    printf("[EVM] path=m/44'/60'/0'/0/0; sender address is unverified; no key loaded\n");
    if (!touch_ready) goto touch_error;
    evm_review_t review;
    evm_review_init(&review);
    unsigned errors = 0;
    uint8_t fingers = 0, last_fingers = 0;
    uint16_t x = 0, y = 0;
    if (!evm_review_render(frame, CAMERA_FRAME_BYTES, &evm_request, &review)) return EVM_PENDING;
    lcd_display_rgb565_be(frame, CAMERA_FRAME_BYTES);
    printf("[EVM] review page=1/5; waiting for manual touch; no automatic response\n");
    while (review.decision == EVM_PENDING) {
        bool valid = touch_read(&fingers, &x, &y);
        if (valid) {
            errors = 0;
            if (fingers && !last_fingers) printf("[Touch] down x=%u y=%u page=%u\n", x, y, review.page + 1);
            last_fingers = fingers;
        } else if (++errors >= 10) goto touch_error;
        if (evm_review_touch(&review, valid, fingers, x, y, time_us_64() / 1000u)) {
            if (review.decision == EVM_PENDING) {
                evm_review_render(frame, CAMERA_FRAME_BYTES, &evm_request, &review);
                lcd_display_rgb565_be(frame, CAMERA_FRAME_BYTES);
                printf("[EVM] review page=%u/5\n", review.page + 1);
            }
        }
        sleep_ms(20);
    }
    printf("[EVM] decision=%s request_id=%s; NOT SIGNED\n",
           review.decision == EVM_CONFIRMED ? "CONFIRMED" : "REJECTED", uuid);
    return review.decision;
touch_error:
    printf("[Touch] unavailable/read error; review ABORTED; no receipt; RESET to retry\n");
    lcd_clear(LCD_BLACK);
    lcd_draw_text(10, 70, "TOUCH ERROR", 1, LCD_RED, LCD_BLACK);
    lcd_draw_text(10, 110, "NO RESPONSE SENT", 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(10, 150, "RESET TO RETRY", 1, LCD_WHITE, LCD_BLACK);
    sleep_ms(5000);
    return EVM_PENDING;
}

static void show_ur_echo(uint8_t *frame, bool evm)
{
    evm_decision_t decision = EVM_PENDING;
    if (evm) {
        decision = show_evm_review(frame);
        if (decision == EVM_PENDING) return;
        size_t size = evm_decision_response(ur_message.cbor, ur_message.length, decision,
                                           ur_message.cbor, sizeof(ur_message.cbor));
        if (!size) { printf("[EVM] receipt encoding failed\n"); return; }
        ur_message.length = size;
        strcpy(ur_message.type, "bytes");
    }
    if (!cw_ur_tx_begin(ur_message.type, ur_message.cbor, ur_message.length)) {
        printf("[UR] response allocation failed\n");
        return;
    }
    printf("[UR] echo ready: type=%s cbor=%u parts=%u interval=%ums hold=%us; NOT A SIGNATURE\n",
           ur_message.type, (unsigned)ur_message.length, cw_ur_tx_count(), UR_FRAME_MS, UR_ECHO_HOLD_SECONDS);
    const uint64_t end = time_us_64() + UR_ECHO_HOLD_SECONDS * 1000000ull;
    unsigned number = 0;
    while (time_us_64() < end) {
        const uint64_t start = time_us_64();
        char part[CW_UR_PART_MAX + 1u], label[22];
        if (!cw_ur_tx_next(part) || !qr_encode_ur(part, &response_symbol)
            || !qr_render_rgb565_be(&response_symbol, frame, CAMERA_FRAME_BYTES, LCD_WIDTH, LCD_HEIGHT)) {
            printf("[UR] response encode/render failed\n");
            break;
        }
        ++number;
        snprintf(label, sizeof(label), "FRAME %u %us", number,
                 (unsigned)((end - start + 999999u) / 1000000u));
        scan_preview_compose(frame, CAMERA_FRAME_BYTES, evm ?
            (decision == EVM_CONFIRMED ? "CONFIRMED / NO SIGN" : "REJECTED / NO SIGN") : "UR ECHO / TEST", label);
        lcd_display_rgb565_be(frame, CAMERA_FRAME_BYTES);
        if (number % 12u == 1u) printf("[UR] echo frame=%u arena_peak=%u/%u\n", number,
            (unsigned)cw_ur_memory_peak(), CW_UR_ARENA_BYTES);
        while (time_us_64() - start < UR_FRAME_MS * 1000u) sleep_ms(10);
    }
    cw_ur_tx_end();
    printf("[UR] resuming scanner; arena_used=%u\n", (unsigned)cw_ur_memory_used());
}

int main(void)
{
    stdio_init_all();
    lcd_init();
    printf("[CamWallet] TOUCH + EVM REVIEW V1 (NO SIGNING)\n");
    touch_ready = touch_init();
    printf("[UR] cUR ready: part=%u message=%u fragments=%u arena=%u timeout=%ums\n",
           CW_UR_PART_MAX, CW_UR_MESSAGE_MAX, CW_UR_SEQUENCE_MAX, CW_UR_ARENA_BYTES, CW_UR_TIMEOUT_MS);
    printf("[LCD] preview: composed status bars, one frame transfer\n");
    lcd_draw_text(21, 32, "CamWallet", 2, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(21, 96, "STARTING CAMERA", 1, LCD_WHITE, LCD_BLACK);

    if (!qr_scanner_init(CAMERA_WIDTH, CAMERA_HEIGHT)) {
        lcd_clear(LCD_BLACK);
        lcd_draw_text(10, 80, "QR MEMORY ERROR", 1, LCD_RED, LCD_BLACK);
        while (true) {
            printf("[QR] scanner allocation failed; check RAM and stack reservation\n");
            sleep_ms(1000);
        }
    }
    printf("[QR] scanner ready: %ux%u gray8, max_payload=%u, response=%us\n",
           CAMERA_WIDTH, CAMERA_HEIGHT, QR_PAYLOAD_MAX_BYTES, QR_ECHO_HOLD_SECONDS);

    uint16_t chip_id = 0;
    if (!camera_init(&chip_id)) {
        show_camera_error(camera_last_error());
        while (true) {
            printf("[Camera] init failed: %s; chip_id=0x%04x; check cable with power off, then reset\n",
                   camera_last_error(), chip_id);
            sleep_ms(1000);
        }
    }

    uint32_t frame_count = 0;
    uint32_t window_frames = 0;
    uint64_t last_report = time_us_64();
    bool showing_error = false;
    bool capture_running = false;
    uint capture_index = 0;
    const char *hint = "SCAN QR";
    uint64_t hint_until = 0;
    uint64_t next_rejection_log = 0;
    while (true) {
        /* Start on the first iteration, or retry after an aborted capture. */
        if (!capture_running) {
            if (!camera_capture_start(camera_frames[capture_index], CAMERA_FRAME_BYTES)) {
                printf("[Camera] start failed: %s\n", camera_last_error());
                show_camera_error(camera_last_error());
                showing_error = true;
                sleep_ms(250);
                continue;
            }
            capture_running = true;
        }

        const uint64_t wait_start = time_us_64();
        uint32_t capture_us;
        if (!camera_capture_wait(2000, &capture_us)) {
            capture_running = false;
            if (!showing_error) {
                show_camera_error(camera_last_error());
                showing_error = true;
            }
            sleep_ms(250);
            continue;
        }
        capture_running = false;
        const uint32_t wait_us = (uint32_t)(time_us_64() - wait_start);
        const uint display_index = capture_index;
        capture_index ^= 1u;

        /* Arm the next frame BEFORE QR decoding and the blocking SPI write.
         * DMA owns only the other buffer; this completed one belongs to the
         * main loop for decoding, status composition, and LCD transmission.
         */
        if (!camera_capture_start(camera_frames[capture_index], CAMERA_FRAME_BYTES)) {
            printf("[Camera] restart failed: %s\n", camera_last_error());
            show_camera_error(camera_last_error());
            showing_error = true;
            sleep_ms(250);
            continue;
        }
        capture_running = true;

        const uint64_t decode_start = time_us_64();
        const qr_scan_status_t scan_status = qr_scan_rgb565_be(
            camera_frames[display_index], CAMERA_FRAME_BYTES, &received_payload);
        const qr_scan_info_t scan_info = qr_last_scan_info();
        const uint32_t decode_us = (uint32_t)(time_us_64() - decode_start);
        if (cw_ur_expire(time_us_64() / 1000u)) {
            printf("[UR] receive timeout; session reset, scan the new request again\n");
            hint = "UR TIMEOUT / RESCAN";
            hint_until = time_us_64() + 2000000u;
        }
        const bool is_ur = scan_status == QR_SCAN_OK
            && cw_ur_is_part(received_payload.bytes, received_payload.length);
        if (is_ur) {
            cw_ur_status_t status = cw_ur_receive(received_payload.bytes, received_payload.length,
                time_us_64() / 1000u, &ur_message);
            hint = cw_ur_status_text(status);
            hint_until = time_us_64() + 2000000u;
            bool evm = false;
            const char *error = NULL;
            if (status == CW_UR_COMPLETE) {
                printf("[UR] complete: type=%s cbor=%u arena_peak=%u/%u\n", ur_message.type,
                    (unsigned)ur_message.length, (unsigned)cw_ur_memory_peak(), CW_UR_ARENA_BYTES);
                error = prepare_ur_response(&evm);
                if (!error) {
                    if (!camera_capture_wait(2000, NULL)) printf("[Camera] drain before UR: %s\n", camera_last_error());
                    capture_running = false;
                    show_ur_echo(camera_frames[display_index], evm);
                    hint = "SCAN QR"; hint_until = 0; showing_error = false;
                    window_frames = 0; last_report = time_us_64();
                    continue;
                }
                hint = error;
            }
            if (error || time_us_64() >= next_rejection_log) {
                printf("[UR] %s recovered=%u/%u arena=%u/%u\n", hint, cw_ur_received(), cw_ur_expected(),
                    (unsigned)cw_ur_memory_used(), CW_UR_ARENA_BYTES);
                next_rejection_log = time_us_64() + 1000000u;
            }
        }
        if (scan_status == QR_SCAN_OK && !is_ur && !cw_ur_active()) {
            last_payload = received_payload;
            have_last_payload = true;
            last_received_at = time_us_64();
            next_result_report = last_received_at + 5000000u;
            /* Decoding and screen encoding are separate results. Always report
             * the decoded bytes even if building the response should fail.
             */
            log_payload(&received_payload, "received");
        }
        if (scan_status == QR_SCAN_OK && !is_ur && !cw_ur_active()
            && qr_encode_payload(&received_payload, &response_symbol)) {
            /* Finish (or timeout/abort) the outstanding DMA before the response
             * screen reuses a camera buffer and pauses the capture loop.
             */
            if (!camera_capture_wait(2000, NULL)) {
                printf("[Camera] capture drained with error before echo: %s\n",
                       camera_last_error());
            }
            capture_running = false;
            show_echo(camera_frames[display_index], &received_payload);
            hint = "SCAN QR";
            hint_until = 0;
            showing_error = false;
            window_frames = 0;
            last_report = time_us_64();
            continue;
        }

        const uint64_t hint_now = time_us_64();
        if (is_ur || cw_ur_active()) {
            /* Do not echo individual UR fragments or interrupt an active UR
             * collection with an unrelated plain-text QR. */
            if (!is_ur && hint_now >= hint_until) hint = "UR RECEIVING";
        } else if (scan_status != QR_SCAN_NONE) {
            hint = scan_status == QR_SCAN_OK ? "QR ENCODE ERROR" : scan_hint(scan_status);
            hint_until = hint_now + 2000000u;
            if (hint_now >= next_rejection_log) {
                printf("[QR] rejected: %s; no partial payload returned\n", hint);
                log_scan_info(scan_status, &scan_info);
                next_rejection_log = hint_now + 1000000u;
            }
        } else if (hint_now >= hint_until) {
            hint = "NO QR FOUND";
        }

        const uint64_t lcd_start = time_us_64();
        char scan_label[22];
        snprintf(scan_label, sizeof(scan_label), "FOUND %u READ %u",
                 scan_info.candidates, scan_info.decoded);
        if (cw_ur_active()) snprintf(scan_label, sizeof(scan_label), "UR %u/%u 30s LIMIT",
            cw_ur_received(), cw_ur_expected());
        /* Decode the untouched camera frame first, then compose both bars in
         * RAM. One LCD write avoids showing camera -> blank bar -> text on
         * every frame, with no additional framebuffer allocation.
         */
        scan_preview_compose(camera_frames[display_index], CAMERA_FRAME_BYTES,
                             hint, scan_label);
        lcd_display_rgb565_be(camera_frames[display_index], CAMERA_FRAME_BYTES);
        const uint64_t lcd_end = time_us_64();
        showing_error = false;
        ++frame_count;
        ++window_frames;

        const uint64_t now = lcd_end;
        if (now - last_report >= 1000000u) {
            const unsigned long fps_x10 = (unsigned long)
                ((uint64_t)window_frames * 10000000u / (now - last_report));
            printf("[Camera] id=0x%04x 240x320 RGB565; frames=%lu fps=%lu.%lu capture=%lums wait=%lums lcd=%lums qr=%lums sensor=%lums\n",
                   chip_id, (unsigned long)frame_count, fps_x10 / 10u, fps_x10 % 10u,
                   (unsigned long)(capture_us / 1000u),
                   (unsigned long)(wait_us / 1000u),
                   (unsigned long)((lcd_end - lcd_start) / 1000u),
                   (unsigned long)(decode_us / 1000u),
                   (unsigned long)(camera_sensor_period_us() / 1000u));
            log_scan_info(scan_status, &scan_info);
            if (!have_last_payload) {
                printf("[QR] no successful decode yet\n");
            }
            report_last_result();
            last_report = now;
            window_frames = 0;
        }
    }
}
