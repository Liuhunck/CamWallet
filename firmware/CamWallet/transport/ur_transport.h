#ifndef CAMWALLET_UR_TRANSPORT_H
#define CAMWALLET_UR_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CW_UR_MESSAGE_MAX 768u
#define CW_UR_PART_MAX 128u
#define CW_UR_TYPE_MAX 16u
#define CW_UR_FRAGMENT_BYTES 24u
#define CW_UR_SEQUENCE_MAX 32u
#define CW_UR_TIMEOUT_MS 30000u

typedef enum {
    CW_UR_MORE, CW_UR_COMPLETE, CW_UR_REJECTED, CW_UR_TOO_LARGE,
    CW_UR_NO_MEMORY, CW_UR_CHECKSUM_ERROR
} cw_ur_status_t;
typedef struct {
    char type[CW_UR_TYPE_MAX + 1u];
    size_t length;
    uint8_t cbor[CW_UR_MESSAGE_MAX];
} cw_ur_message_t;

/* One RX or TX session, main core only. Result is copied out before freeing
 * cUR's state. Unknown payload types are dispatched by the application. */
bool cw_ur_is_part(const uint8_t *data, size_t length);
cw_ur_status_t cw_ur_receive(const uint8_t *data, size_t length, uint64_t now_ms,
                            cw_ur_message_t *out);
void cw_ur_reset(void);
bool cw_ur_expire(uint64_t now_ms);
bool cw_ur_active(void);
unsigned cw_ur_received(void);
unsigned cw_ur_expected(void);
const char *cw_ur_status_text(cw_ur_status_t status);
bool cw_ur_tx_begin(const char *type, const uint8_t *cbor, size_t length);
bool cw_ur_tx_next(char out[CW_UR_PART_MAX + 1u]);
unsigned cw_ur_tx_count(void);
void cw_ur_tx_end(void);
#endif
