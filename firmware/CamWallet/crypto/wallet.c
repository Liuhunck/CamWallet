#include "wallet.h"
#include "secp256k1.h"
#include "secp256k1_preallocated.h"
#include "secp256k1_recovery.h"
#include "hmac.h"
#include "sha3.h"
#include "sha2.h"
#include "ripemd160.h"
#include "memzero.h"
#include <string.h>

static _Alignas(16) uint8_t context_memory[1024];
static secp256k1_context *context;
static uint8_t secret[32], address[20];
static uint32_t fingerprint;
static bool ready;

void wallet_keccak(const uint8_t *data, size_t size, uint8_t out[32]) { keccak_256(data, size, out); }
void wallet_clear(void)
{
    ready = false;
    memzero(secret, sizeof(secret)); memzero(address, sizeof(address)); fingerprint = 0;
    if (context) secp256k1_context_preallocated_destroy(context);
    context = NULL; memzero(context_memory, sizeof(context_memory));
}
static bool public_bytes(const uint8_t key[32], uint8_t *out, bool compressed)
{
    secp256k1_pubkey pub;
    size_t size = compressed ? 33 : 65;
    bool ok = secp256k1_ec_pubkey_create(context, &pub, key)
        && secp256k1_ec_pubkey_serialize(context, out, &size, &pub,
                  compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED);
    memzero(&pub, sizeof(pub)); return ok;
}
bool wallet_create(const uint8_t *seed, size_t size, const uint8_t blind[32])
{
    wallet_clear();
    if (!seed || size < 16 || size > 64 || !blind
        || secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE) > sizeof(context_memory)) return false;
    context = secp256k1_context_preallocated_create(context_memory, SECP256K1_CONTEXT_NONE);
    uint8_t node[64] = {0}, material[37] = {0}, child[64] = {0}, pub[65] = {0}, hash[32] = {0};
    bool ok = false;
    if (!context || !secp256k1_context_randomize(context, blind)) goto done;
    hmac_sha512((const uint8_t *)"Bitcoin seed", 12, seed, (uint32_t)size, node);
    if (!secp256k1_ec_seckey_verify(context, node) || !public_bytes(node, pub, true)) goto done;
    sha256_Raw(pub, 33, hash); ripemd160(hash, 32, pub);
    fingerprint = ((uint32_t)pub[0] << 24) | ((uint32_t)pub[1] << 16) | ((uint32_t)pub[2] << 8) | pub[3];
    static const uint32_t path[] = {0x8000002c, 0x8000003c, 0x80000000, 0, 0};
    for (unsigned i = 0; i < 5; ++i) {
        if (path[i] & 0x80000000) { material[0] = 0; memcpy(material + 1, node, 32); }
        else if (!public_bytes(node, material, true)) goto done;
        for (unsigned j = 0; j < 4; ++j) material[33 + j] = (uint8_t)(path[i] >> (24 - 8*j));
        hmac_sha512(node + 32, 32, material, sizeof(material), child);
        /* For the fixed path, an invalid child aborts instead of silently
         * changing the index and signing with a different derivation path. */
        if (!secp256k1_ec_seckey_tweak_add(context, node, child)) goto done;
        memcpy(node + 32, child + 32, 32);
    }
    if (!public_bytes(node, pub, false)) goto done;
    wallet_keccak(pub + 1, 64, hash);
    memcpy(secret, node, 32); memcpy(address, hash + 12, 20); ready = true; ok = true;
done:
    memzero(node, sizeof(node)); memzero(material, sizeof(material)); memzero(child, sizeof(child));
    memzero(pub, sizeof(pub)); memzero(hash, sizeof(hash));
    if (!ok) wallet_clear();
    return ok;
}
bool wallet_address(uint8_t out[20])
{
    if (!ready || !out) return false;
    memcpy(out, address, 20); return true;
}
const char *wallet_check(const evm_request_t *r)
{
    if (!ready || !r) return "NO ACCOUNT";
    if (r->chain_id != WALLET_CHAIN_ID) return "SEPOLIA ONLY";
    if (!r->has_address || memcmp(r->address, address, 20)) return "SENDER MISMATCH";
    if (r->has_fingerprint && r->fingerprint != fingerprint) return "FINGERPRINT MISMATCH";
    return NULL;
}
size_t wallet_sign(const uint8_t *data, size_t size, bool approved, uint8_t *out, size_t capacity)
{
    evm_request_t request;
    if (!approved || !out || capacity < WALLET_SIGNATURE_CBOR_SIZE
        || evm_parse_request(data, size, &request) || wallet_check(&request)) return 0;
    uint8_t hash[32], signature[65] = {0}, result[WALLET_SIGNATURE_CBOR_SIZE];
    secp256k1_ecdsa_recoverable_signature recoverable;
    secp256k1_ecdsa_signature normal;
    secp256k1_pubkey pub;
    int recovery = -1;
    size_t written = 0;
    wallet_keccak(request.sign_data, request.sign_size, hash);
    if (!secp256k1_ecdsa_sign_recoverable(context, &recoverable, hash, secret, NULL, NULL)
        || !secp256k1_ecdsa_recoverable_signature_serialize_compact(context, signature, &recovery, &recoverable)
        || recovery < 0 || recovery > 1
        || !secp256k1_ecdsa_recoverable_signature_convert(context, &normal, &recoverable)
        || !secp256k1_ec_pubkey_create(context, &pub, secret)
        || !secp256k1_ecdsa_verify(context, &normal, hash, &pub)) goto done;
    signature[64] = (uint8_t)recovery; /* Type 2 yParity, 0 or 1; low-S from libsecp. */
    const uint8_t prefix[] = {0xa2, 1, 0xd8, 37, 0x50};
    memcpy(result, prefix, 5); memcpy(result + 5, request.request_id, 16);
    result[21] = 2; result[22] = 0x58; result[23] = 65;
    memcpy(result + 24, signature, 65);
    memcpy(out, result, sizeof(result)); written = sizeof(result);
done:
    memzero(hash, sizeof(hash)); memzero(signature, sizeof(signature)); memzero(result, sizeof(result));
    memzero(&recoverable, sizeof(recoverable)); memzero(&normal, sizeof(normal)); memzero(&pub, sizeof(pub));
    return written;
}
