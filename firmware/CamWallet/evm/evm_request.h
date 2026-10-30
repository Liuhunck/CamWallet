#ifndef CAMWALLET_EVM_REQUEST_H
#define CAMWALLET_EVM_REQUEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t request_id[16];
    uint8_t to[20];
    uint8_t value[32];
    uint8_t max_total_fee[32];
    uint8_t address[20];
    bool has_address;
    bool has_fingerprint;
    uint32_t fingerprint;
    /* Borrowed from the immutable input CBOR; valid until that buffer changes. */
    const uint8_t *sign_data;
    size_t sign_size;
    bool signing; /* Set only by the application after wallet validation. */
    uint64_t chain_id, nonce, max_fee_per_gas, priority_fee_per_gas;
    uint32_t gas_limit;
} evm_request_t;

/* ERC-4527 subset: required UUID, dataType=4, m/44'/60'/0'/0/0,
 * unsigned EIP-1559 native transfer, empty data and access list. Parses only;
 * does not trust/verify the optional sender address and never signs. */
const char *evm_parse_request(const uint8_t *cbor, size_t size, evm_request_t *out);
void evm_uint256_decimal(const uint8_t number[32], char out[79]);
void evm_hex(const uint8_t *bytes, size_t size, char *out);
bool cw_cbor_unwrap_bytes(const uint8_t *cbor, size_t size,
                          const uint8_t **bytes, size_t *length);
size_t cw_cbor_wrap_bytes(const uint8_t *bytes, size_t size, uint8_t *out, size_t capacity);
#endif
