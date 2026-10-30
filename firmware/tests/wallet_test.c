#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "wallet.h"

int main(void)
{
    uint8_t seed[32], blind[32] = {1}, first[20], second[20], hash[32];
    for (unsigned i = 0; i < sizeof(seed); ++i) seed[i] = (uint8_t)i;
    const uint8_t empty_keccak[] = {
        0xc5,0xd2,0x46,0x01,0x86,0xf7,0x23,0x3c,0x92,0x7e,0x7d,0xb2,0xdc,0xc7,0x03,0xc0,
        0xe5,0x00,0xb6,0x53,0xca,0x82,0x27,0x3b,0x7b,0xfa,0xd8,0x04,0x5d,0x85,0xa4,0x70};
    wallet_keccak((const uint8_t *)"", 0, hash);
    assert(!memcmp(hash, empty_keccak, 32)); /* Keccak, NOT SHA3-256. */
    assert(wallet_create(seed, sizeof(seed), blind)); assert(wallet_address(first));
    wallet_clear(); assert(!wallet_address(second));
    assert(wallet_create(seed, sizeof(seed), blind)); assert(wallet_address(second));
    assert(!memcmp(first, second, 20));
    seed[0] ^= 1;
    assert(wallet_create(seed, sizeof(seed), blind)); assert(wallet_address(second));
    assert(memcmp(first, second, 20));
    assert(!wallet_create(seed, 15, blind)); assert(!wallet_address(second));
    assert(!wallet_create(seed, sizeof(seed), NULL)); assert(!wallet_address(second));
    uint8_t output[89]; memset(output, 0xa5, sizeof(output));
    assert(!wallet_sign(NULL, 0, true, output, sizeof(output)));
    assert(!wallet_sign(NULL, 0, false, output, sizeof(output)));
    for (unsigned i = 0; i < sizeof(output); ++i) assert(output[i] == 0xa5);
    wallet_clear(); puts("Keccak vectors, account replacement/clear, failed initialization and signing gate passed");
    return 0;
}
