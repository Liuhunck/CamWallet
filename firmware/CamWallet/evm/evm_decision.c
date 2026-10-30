#include "evm_decision.h"
#include "evm_request.h"
#include "sha256/sha256.h"
#include <string.h>

size_t evm_decision_response(const uint8_t *data, size_t size,
                           evm_decision_t decision, uint8_t *out, size_t capacity)
{
    evm_request_t request;
    if (!data || !out || capacity < EVM_DECISION_CBOR_SIZE
        || (decision != EVM_REJECTED && decision != EVM_CONFIRMED)
        || evm_parse_request(data, size, &request)) return 0;
    /* Build locally so callers may replace the original request in-place. */
    uint8_t result[EVM_DECISION_CBOR_SIZE] = {0x58, 55, 'C','W','R','E','V', 1, 0};
    result[8] = decision == EVM_CONFIRMED ? 1 : 0;
    memcpy(result + 9, request.request_id, 16);
    CRYAL_SHA256_CTX hash;
    ur_bundled_sha256_init(&hash);
    ur_bundled_sha256_update(&hash, data, size);
    ur_bundled_sha256_final(&hash, result + 25);
    memcpy(out, result, sizeof(result));
    return sizeof(result);
}
