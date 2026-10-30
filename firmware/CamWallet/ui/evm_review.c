#include "evm_review.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { WAIT_UP, READY, DOWN, RELEASE };

void evm_review_init(evm_review_t *s) { memset(s, 0, sizeof(*s)); }

review_button_t evm_review_hit(unsigned page, uint16_t x, uint16_t y)
{
    if (x >= 240 || y >= 320) return REVIEW_NO_BUTTON;
    if (page && x >= 160 && x < 236 && y >= 4 && y < 36) return REVIEW_BACK;
    if (y < 270 || y >= 316) return REVIEW_NO_BUTTON;
    if (x >= 4 && x < 112) return REVIEW_REJECT;
    if (x >= 128 && x < 236) return REVIEW_NEXT;
    return REVIEW_NO_BUTTON;
}

bool evm_review_touch(evm_review_t *s, bool valid, uint8_t fingers,
                      uint16_t x, uint16_t y, uint64_t now)
{
    if (s->decision != EVM_PENDING) return false;
    if (!valid || fingers > 1 || (fingers && (x >= 240 || y >= 320))) {
        s->touch_state = WAIT_UP; s->timing = false; return false;
    }
    switch (s->touch_state) {
    case WAIT_UP:
        if (fingers) { s->timing = false; break; }
        if (!s->timing) { s->since = now; s->timing = true; }
        if (now - s->since >= 60) s->touch_state = READY;
        break;
    case READY:
        if (fingers) {
            s->button = evm_review_hit(s->page, x, y);
            s->since = now; s->touch_state = DOWN;
        }
        break;
    case DOWN:
        if (now - s->since > 2000) s->button = REVIEW_NO_BUTTON;
        if (fingers) {
            if (evm_review_hit(s->page, x, y) != s->button) s->button = REVIEW_NO_BUTTON;
        } else {
            if (now - s->since < 40) s->button = REVIEW_NO_BUTTON;
            s->since = now; s->touch_state = RELEASE;
        }
        break;
    case RELEASE:
        if (fingers) { s->touch_state = WAIT_UP; s->timing = false; break; }
        if (now - s->since < 60) break;
        s->touch_state = READY;
        switch (s->button) {
        case REVIEW_BACK: if (s->page) --s->page; else return false; break;
        case REVIEW_REJECT: s->decision = EVM_REJECTED; break;
        case REVIEW_NEXT:
            if (s->page + 1 < REVIEW_PAGE_COUNT) ++s->page;
            else s->decision = EVM_CONFIRMED;
            break;
        default: return false;
        }
        return true;
    }
    return false;
}

static void split(char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1], unsigned row, const char *text)
{
    while (*text && row < REVIEW_ROWS) {
        size_t n = strlen(text);
        if (n > REVIEW_COLUMNS) n = REVIEW_COLUMNS;
        memcpy(rows[row], text, n); rows[row++][n] = 0; text += n;
    }
}

void evm_review_lines(const evm_request_t *r, unsigned page,
                      char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1])
{
    char text[100], digits[79];
    memset(rows, 0, REVIEW_ROWS * (REVIEW_COLUMNS + 1));
    const bool eth = r->chain_id == 1 || r->chain_id == 11155111;
    switch (page) {
    case 0:
        split(rows, 0, "NATIVE TRANSFER");
        split(rows, 2, r->chain_id == 1 ? "Ethereum mainnet" :
              r->chain_id == 11155111 ? "Sepolia / TEST ETH" : "UNKNOWN NETWORK");
        split(rows, 3, "CHAIN ID:");
        snprintf(text, sizeof(text), "%" PRIu64, r->chain_id); split(rows, 4, text);
        split(rows, 6, "NONCE:");
        snprintf(text, sizeof(text), "%" PRIu64, r->nonce); split(rows, 7, text);
        split(rows, 9, "TYPE 2 / EIP-1559");
        split(rows, 10, "NO DATA / NO ACCESS");
        split(rows, 11, "REVIEW ONLY / NO SIGN");
        break;
    case 1:
        split(rows, 0, "RECIPIENT (FULL):");
        strcpy(text, "0x"); evm_hex(r->to, 20, text + 2); split(rows, 1, text);
        if (r->has_address) {
            split(rows, 5, "FROM (UNVERIFIED):");
            strcpy(text, "0x"); evm_hex(r->address, 20, text + 2); split(rows, 6, text);
        } else split(rows, 5, "FROM: NOT PROVIDED");
        split(rows, 10, "NO KEY LOADED");
        break;
    case 2:
        split(rows, 0, eth ? "AMOUNT (WEI):" : "AMOUNT (BASE UNITS):");
        evm_uint256_decimal(r->value, digits); split(rows, 1, digits);
        if (eth) {
            split(rows, 6, "AMOUNT (ETH):");
            size_t n = strlen(digits);
            if (n <= 18) {
                strcpy(text, "0."); memset(text + 2, '0', 18 - n);
                strcpy(text + 2 + 18 - n, digits);
            } else {
                memcpy(text, digits, n - 18); text[n - 18] = '.';
                strcpy(text + n - 17, digits + n - 18);
            }
            /* Decimal string arithmetic; retain all 18 fractional digits. */
            split(rows, 7, text);
        } else {
            split(rows, 6, "UNKNOWN NATIVE UNIT");
            split(rows, 8, "CHECK CHAIN + UNITS");
            split(rows, 9, "BEFORE CONFIRMING");
        }
        break;
    case 3:
        split(rows, 0, eth ? "MAX FEE (WEI):" : "MAX FEE BASE UNITS:");
        evm_uint256_decimal(r->max_total_fee, text); split(rows, 1, text);
        split(rows, 3, "GAS LIMIT:");
        snprintf(text, sizeof(text), "%" PRIu32, r->gas_limit); split(rows, 4, text);
        split(rows, 5, "MAX FEE / GAS:");
        snprintf(text, sizeof(text), "%" PRIu64, r->max_fee_per_gas); split(rows, 6, text);
        split(rows, 7, "PRIORITY FEE / GAS:");
        snprintf(text, sizeof(text), "%" PRIu64, r->priority_fee_per_gas); split(rows, 8, text);
        split(rows, 10, "PER GAS: BASE UNITS");
        split(rows, 11, "CAP, NOT ACTUAL FEE");
        break;
    case 4:
        split(rows, 0, "CONFIRM THIS REVIEW?");
        split(rows, 2, "REQUEST ID:");
        evm_hex(r->request_id, 16, text); split(rows, 3, text);
        split(rows, 6, "CONFIRM / REJECT");
        split(rows, 7, "RETURNS A RECEIPT");
        split(rows, 9, "NO SIGNATURE");
        split(rows, 10, "NO BROADCAST");
        if (!eth) split(rows, 11, "UNKNOWN NETWORK");
        break;
    }
}
