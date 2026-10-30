#include "evm_request.h"
#include <string.h>

typedef struct { const uint8_t *p; size_t left; } cursor_t;

/* Definite-length, minimally encoded CBOR. No recursion or allocation. */
static bool head(cursor_t *c, unsigned major, uint64_t *value)
{
    if (!c->left || c->p[0] >> 5 != major) return false;
    unsigned ai = c->p[0] & 31u;
    ++c->p; --c->left;
    if (ai < 24) { *value = ai; return true; }
    if (ai > 27) return false;
    unsigned count = 1u << (ai - 24u);
    if (count > c->left) return false;
    uint64_t n = 0;
    for (unsigned i = 0; i < count; ++i) n = (n << 8) | *c->p++;
    c->left -= count;
    if (n < 24 || (count > 1 && n <= UINT8_MAX)
        || (count > 2 && n <= UINT16_MAX) || (count > 4 && n <= UINT32_MAX)) return false;
    *value = n;
    return true;
}
static bool span(cursor_t *c, unsigned major, const uint8_t **data, size_t *size)
{
    uint64_t n;
    if (!head(c, major, &n) || n > c->left) return false;
    *data = c->p; *size = (size_t)n;
    c->p += *size; c->left -= *size;
    return true;
}
bool cw_cbor_unwrap_bytes(const uint8_t *cbor, size_t size,
                          const uint8_t **bytes, size_t *length)
{
    if (!cbor || !bytes || !length) return false;
    cursor_t c = { cbor, size };
    return span(&c, 2, bytes, length) && c.left == 0;
}
size_t cw_cbor_wrap_bytes(const uint8_t *bytes, size_t size, uint8_t *out, size_t capacity)
{
    size_t header = size < 24 ? 1 : size <= 255 ? 2 : 3;
    if (!bytes || !out || size > 65535 || capacity < header || size > capacity - header) return 0;
    memmove(out + header, bytes, size);
    if (header == 1) out[0] = (uint8_t)(0x40u + size);
    else if (header == 2) { out[0] = 0x58; out[1] = (uint8_t)size; }
    else { out[0] = 0x59; out[1] = (uint8_t)(size >> 8); out[2] = (uint8_t)size; }
    return size + header;
}

static bool path(cursor_t *c, evm_request_t *out)
{
    uint64_t n, tag;
    if (!head(c, 6, &tag) || tag != 304 || !head(c, 5, &n) || n < 1 || n > 3) return false;
    unsigned seen = 0;
    for (uint64_t i = 0; i < n; ++i) {
        uint64_t key, value;
        if (!head(c, 0, &key) || key < 1 || key > 3 || (seen & (1u << key))) return false;
        seen |= 1u << key;
        if (key == 1) {
            static const uint32_t indexes[] = {44, 60, 0, 0, 0};
            if (!head(c, 4, &value) || value != 10) return false;
            for (unsigned j = 0; j < 5; ++j) {
                if (!head(c, 0, &value) || value != indexes[j]
                    || !head(c, 7, &value) || value != (j < 3 ? 21u : 20u)) return false;
            }
        } else {
            if (!head(c, 0, &value) || (key == 2 ? value > UINT32_MAX : value != 5)) return false;
            if (key == 2) { out->has_fingerprint = true; out->fingerprint = (uint32_t)value; }
        }
    }
    return (seen & 2u) != 0;
}

typedef struct { const uint8_t *data; size_t size; bool list; } rlp_t;
static bool rlp_next(cursor_t *c, rlp_t *out)
{
    if (!c->left) return false;
    uint8_t first = *c->p++;
    --c->left;
    out->list = first >= 0xc0;
    if (first < 0x80) { out->data = c->p - 1; out->size = 1; return true; }
    unsigned prefix = out->list ? 0xc0 : 0x80;
    size_t size;
    if (first - prefix <= 55) size = first - prefix;
    else {
        unsigned count = first - prefix - 55;
        if (count > sizeof(size_t) || count > c->left || c->p[0] == 0) return false;
        size = 0;
        for (unsigned i = 0; i < count; ++i) size = (size << 8) | *c->p++;
        c->left -= count;
        if (size < 56) return false;
    }
    if (size > c->left || (!out->list && size == 1 && c->p[0] < 0x80)) return false;
    out->data = c->p; out->size = size;
    c->p += size; c->left -= size;
    return true;
}
static bool rlp_number(const rlp_t *item, uint64_t *out)
{
    if (item->list || item->size > 8 || (item->size && item->data[0] == 0)) return false;
    *out = 0;
    for (size_t i = 0; i < item->size; ++i) *out = (*out << 8) | item->data[i];
    return true;
}
static const char *transaction(const uint8_t *bytes, size_t size, evm_request_t *out)
{
    if (size < 2 || bytes[0] != 2) return "EVM NEEDS TYPE 2";
    cursor_t c = {bytes + 1, size - 1};
    rlp_t list, fields[9];
    if (!rlp_next(&c, &list) || !list.list || c.left) return "EVM INVALID RLP";
    c = (cursor_t){list.data, list.size};
    for (unsigned i = 0; i < 9; ++i) if (!rlp_next(&c, fields + i)) return "EVM INVALID RLP";
    if (c.left) return "EVM UNSIGNED ONLY";
    uint64_t chain, gas;
    if (!rlp_number(fields, &chain) || !chain || chain != out->chain_id) return "EVM CHAIN MISMATCH";
    if (!rlp_number(fields + 1, &out->nonce)
        || !rlp_number(fields + 2, &out->priority_fee_per_gas)
        || !rlp_number(fields + 3, &out->max_fee_per_gas)
        || !rlp_number(fields + 4, &gas) || gas < 21000 || gas > UINT32_MAX
        || out->max_fee_per_gas < out->priority_fee_per_gas) return "EVM INVALID FEES";
    out->gas_limit = (uint32_t)gas;
    if (fields[5].list || fields[5].size != 20) return "EVM TRANSFER ONLY";
    if (fields[6].list || fields[6].size > 32 || (fields[6].size && fields[6].data[0] == 0))
        return "EVM INVALID VALUE";
    if (fields[7].list || fields[7].size || !fields[8].list || fields[8].size)
        return "EVM DATA UNSUPPORTED";
    memcpy(out->to, fields[5].data, 20);
    memcpy(out->value + 32 - fields[6].size, fields[6].data, fields[6].size);
    uint64_t fee = out->max_fee_per_gas;
    for (unsigned i = 0; i < 8; ++i) { out->max_total_fee[31 - i] = (uint8_t)fee; fee >>= 8; }
    uint64_t carry = 0;
    for (unsigned i = 32; i > 0; --i) {
        carry += (uint64_t)out->max_total_fee[i - 1] * out->gas_limit;
        out->max_total_fee[i - 1] = (uint8_t)carry;
        carry >>= 8;
    }
    return NULL;
}
const char *evm_parse_request(const uint8_t *cbor, size_t size, evm_request_t *out)
{
    if (!cbor || !out) return "EVM INVALID REQUEST";
    memset(out, 0, sizeof(*out));
    out->chain_id = 1;
    cursor_t c = {cbor, size};
    uint64_t count;
    if (!head(&c, 5, &count) || count < 4 || count > 7) return "EVM INVALID CBOR";
    const uint8_t *sign_data = NULL;
    size_t sign_size = 0;
    unsigned seen = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key, value;
        const uint8_t *data;
        size_t length;
        if (!head(&c, 0, &key) || key < 1 || key > 7 || (seen & (1u << key))) return "EVM INVALID FIELDS";
        seen |= 1u << key;
        switch (key) {
        case 1:
            if (!head(&c, 6, &value) || value != 37 || !span(&c, 2, &data, &length) || length != 16)
                return "EVM INVALID UUID";
            memcpy(out->request_id, data, 16); break;
        case 2:
            if (!span(&c, 2, &sign_data, &sign_size) || !sign_size || sign_size > 512) return "EVM INVALID TX";
            break;
        case 3:
            if (!head(&c, 0, &value) || value != 4) return "EVM NEEDS DATATYPE 4";
            break;
        case 4:
            if (!head(&c, 0, &out->chain_id) || !out->chain_id) return "EVM INVALID CHAIN";
            break;
        case 5:
            if (!path(&c, out)) return "EVM PATH UNSUPPORTED";
            break;
        case 6:
            if (!span(&c, 2, &data, &length) || length != 20) return "EVM INVALID ADDRESS";
            memcpy(out->address, data, 20); out->has_address = true; break;
        case 7:
            if (!span(&c, 3, &data, &length) || length > 32) return "EVM INVALID ORIGIN";
            for (size_t j = 0; j < length; ++j) if (data[j] < 32 || data[j] > 126) return "EVM INVALID ORIGIN";
            break;
        }
    }
    if (c.left || (seen & 0x2eu) != 0x2eu) return "EVM MISSING FIELDS";
    out->sign_data = sign_data;
    out->sign_size = sign_size;
    return transaction(sign_data, sign_size, out);
}
void evm_uint256_decimal(const uint8_t number[32], char out[79])
{
    uint8_t work[32]; memcpy(work, number, 32);
    char reverse[78]; size_t count = 0;
    bool nonzero;
    do {
        unsigned rem = 0; nonzero = false;
        for (unsigned i = 0; i < 32; ++i) {
            unsigned value = (rem << 8) | work[i];
            work[i] = (uint8_t)(value / 10); rem = value % 10;
            nonzero |= work[i] != 0;
        }
        reverse[count++] = (char)('0' + rem);
    } while (nonzero);
    for (size_t i = 0; i < count; ++i) out[i] = reverse[count - 1 - i];
    out[count] = 0;
}
void evm_hex(const uint8_t *bytes, size_t size, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) { out[2*i] = digits[bytes[i] >> 4]; out[2*i+1] = digits[bytes[i] & 15]; }
    out[2*size] = 0;
}
