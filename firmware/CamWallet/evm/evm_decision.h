#ifndef CAMWALLET_EVM_DECISION_H
#define CAMWALLET_EVM_DECISION_H
#include <stddef.h>
#include <stdint.h>

typedef enum { EVM_PENDING, EVM_REJECTED, EVM_CONFIRMED } evm_decision_t;
#define EVM_DECISION_CBOR_SIZE 57u
/* Custom ur:bytes review receipt, NOT eth-signature or authenticated proof.
 * bstr("CWREV" || 01 || decision[0/1] || UUID[16] || SHA256(request CBOR)[32]).
 * Pending/invalid requests produce no response. */
size_t evm_decision_response(const uint8_t *request, size_t size,
                           evm_decision_t decision, uint8_t *out, size_t capacity);
#endif
