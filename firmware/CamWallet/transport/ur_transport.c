#include "ur_transport.h"
#include "cur_memory.h"
#include "ur_decoder.h"
#include "ur_encoder.h"
#include <string.h>

static ur_decoder_t *decoder;
static ur_encoder_t *encoder;
static uint64_t started_ms;
static char session_type[CW_UR_TYPE_MAX + 1u];
static bool multipart;

bool cw_ur_is_part(const uint8_t *data, size_t length)
{
    return data && length >= 3 && (data[0] == 'u' || data[0] == 'U')
        && (data[1] == 'r' || data[1] == 'R') && data[2] == ':';
}
void cw_ur_reset(void)
{
    ur_decoder_free(decoder);
    decoder = NULL;
    session_type[0] = 0;
    multipart = false;
}
bool cw_ur_expire(uint64_t now_ms)
{
    /* Absolute deadline: repeated/foreign frames cannot keep a session alive. */
    if (decoder && now_ms - started_ms >= CW_UR_TIMEOUT_MS) {
        cw_ur_reset();
        return true;
    }
    return false;
}
bool cw_ur_active(void) { return decoder != NULL; }
unsigned cw_ur_received(void) { return (unsigned)ur_decoder_received_parts_count(decoder); }
unsigned cw_ur_expected(void) { return (unsigned)ur_decoder_expected_part_count(decoder); }

cw_ur_status_t cw_ur_receive(const uint8_t *data, size_t length, uint64_t now_ms,
                            cw_ur_message_t *out)
{
    if (out) out->length = 0;
    if (!out || !cw_ur_is_part(data, length) || length > CW_UR_PART_MAX || encoder)
        return CW_UR_REJECTED;
    char part[CW_UR_PART_MAX + 1u];
    unsigned slashes = 0;
    size_t type_end = 0;
    for (size_t i = 0; i < length; ++i) {
        if (data[i] < 33 || data[i] > 126) return CW_UR_REJECTED;
        part[i] = (data[i] >= 'A' && data[i] <= 'Z') ? (char)(data[i] + 32) : (char)data[i];
        if (part[i] == '/') { if (!type_end) type_end = i; ++slashes; }
    }
    part[length] = 0;
    if (type_end <= 3 || type_end - 3 > CW_UR_TYPE_MAX || slashes < 1 || slashes > 2)
        return CW_UR_REJECTED;
    char type[CW_UR_TYPE_MAX + 1u];
    memcpy(type, part + 3, type_end - 3);
    type[type_end - 3] = 0;
    cw_ur_expire(now_ms);
    if (decoder && (strcmp(type, session_type) || multipart != (slashes == 2)))
        return CW_UR_REJECTED;
    const bool fresh = decoder == NULL;
    if (fresh) {
        decoder = ur_decoder_new();
        if (!decoder) return CW_UR_NO_MEMORY;
        started_ms = now_ms;
        strcpy(session_type, type);
        multipart = slashes == 2;
    }
    ur_decoder_state_t state = ur_decoder_receive_part(decoder, part);
    if (state == UR_DECODER_OK) {
        ur_result_t *result = ur_decoder_get_result(decoder);
        if (!result || !result->cbor_len || result->cbor_len > sizeof(out->cbor)) {
            cw_ur_reset();
            return CW_UR_TOO_LARGE;
        }
        strcpy(out->type, result->type);
        out->length = result->cbor_len;
        memcpy(out->cbor, result->cbor_data, out->length);
        cw_ur_reset();
        return CW_UR_COMPLETE;
    }
    if (state == UR_DECODER_PROCESSING) return CW_UR_MORE;
    cw_ur_status_t status = state == UR_DECODER_ERROR_MEMORY ? CW_UR_NO_MEMORY
        : state == UR_DECODER_ERROR_UNSUPPORTED_SIZE ? CW_UR_TOO_LARGE
        : state == UR_DECODER_ERROR_INVALID_CHECKSUM ? CW_UR_CHECKSUM_ERROR : CW_UR_REJECTED;
    if (fresh || status == CW_UR_NO_MEMORY || ur_decoder_state_is_terminal(state)) cw_ur_reset();
    return status;
}
const char *cw_ur_status_text(cw_ur_status_t status)
{
    switch (status) {
    case CW_UR_MORE: return "UR RECEIVING";
    case CW_UR_COMPLETE: return "UR COMPLETE";
    case CW_UR_TOO_LARGE: return "UR TOO LARGE";
    case CW_UR_NO_MEMORY: return "UR MEMORY LIMIT";
    case CW_UR_CHECKSUM_ERROR: return "UR CRC ERROR";
    default: return "UR FRAME REJECTED";
    }
}
bool cw_ur_tx_begin(const char *type, const uint8_t *cbor, size_t length)
{
    cw_ur_tx_end();
    if (decoder || !type || strlen(type) > CW_UR_TYPE_MAX || !cbor || !length
        || length > CW_UR_MESSAGE_MAX) return false;
    encoder = ur_encoder_new(type, cbor, length, CW_UR_FRAGMENT_BYTES, 0,
                             length < 10 ? length : 10);
    return encoder != NULL;
}
bool cw_ur_tx_next(char out[CW_UR_PART_MAX + 1u])
{
    char *part = NULL;
    if (!out || !encoder || !ur_encoder_next_part(encoder, &part)) return false;
    const size_t length = strlen(part);
    bool ok = length <= CW_UR_PART_MAX;
    if (ok) memcpy(out, part, length + 1u);
    cw_ur_free(part);
    return ok;
}
unsigned cw_ur_tx_count(void) { return (unsigned)ur_encoder_seq_len(encoder); }
void cw_ur_tx_end(void) { ur_encoder_free(encoder); encoder = NULL; }
