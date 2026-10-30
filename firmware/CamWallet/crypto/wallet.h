#ifndef CAMWALLET_WALLET_H
#define CAMWALLET_WALLET_H
#include "evm_request.h"
#define WALLET_CHAIN_ID 11155111u
#define WALLET_SIGNATURE_CBOR_SIZE 89u
/* One RAM-only account, m/44'/60'/0'/0/0. Seed/blinding must come from entropy.
 * Caller wipes both after use. No export of secret material, no persistence. */
bool wallet_create(const uint8_t *seed, size_t seed_size, const uint8_t blind[32]);
void wallet_clear(void);
bool wallet_address(uint8_t out[20]);
const char *wallet_check(const evm_request_t *request);
/* Re-parse and validate the SAME immutable bytes reviewed by the user.
 * False approval, any error, or insufficient capacity => no output. */
size_t wallet_sign(const uint8_t *request, size_t size, bool approved,
                   uint8_t *out, size_t capacity);
void wallet_keccak(const uint8_t *data, size_t size, uint8_t out[32]);
#endif
