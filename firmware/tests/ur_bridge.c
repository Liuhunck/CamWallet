/* Host-only bridge for independent Python/C interoperability tests. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ur_transport.h"
#include "cur_memory.h"
#include "evm_request.h"
#include "ui/evm_review.h"

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static size_t from_hex(const char *hex, uint8_t *out, size_t capacity)
{
    size_t n = strlen(hex);
    if (n % 2 || n / 2 > capacity) return 0;
    for (size_t i = 0; i < n / 2; ++i) {
        int a = nibble(hex[2*i]), b = nibble(hex[2*i+1]);
        if (a < 0 || b < 0) return 0;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return n / 2;
}
int main(int argc, char **argv)
{
    int rc = 2;
    if (argc == 5 && !strcmp(argv[1], "encode")) {
        uint8_t data[CW_UR_MESSAGE_MAX];
        size_t size = from_hex(argv[3], data, sizeof(data));
        unsigned count = (unsigned)strtoul(argv[4], NULL, 10);
        if (!size || !count || count > 2000 || !cw_ur_tx_begin(argv[2], data, size)) goto done;
        for (unsigned i = 0; i < count; ++i) {
            char part[CW_UR_PART_MAX + 1];
            if (!cw_ur_tx_next(part)) goto done;
            puts(part);
        }
        rc = 0;
    } else if (argc == 2 && !strcmp(argv[1], "decode")) {
        char line[512]; cw_ur_message_t message; unsigned tick = 0;
        while (fgets(line, sizeof(line), stdin)) {
            size_t n = strcspn(line, "\r\n");
            cw_ur_status_t status = cw_ur_receive((uint8_t *)line, n, tick++, &message);
            if (status == CW_UR_COMPLETE) {
                char hex[CW_UR_MESSAGE_MAX * 2 + 1]; evm_hex(message.cbor, message.length, hex);
                printf("%s %s\n", message.type, hex); rc = 0; break;
            }
            if (status != CW_UR_MORE) fprintf(stderr, "RX %s\n", cw_ur_status_text(status));
        }
    } else if (argc == 4 && !strcmp(argv[1], "decision")) {
        uint8_t data[CW_UR_MESSAGE_MAX];
        size_t size = from_hex(argv[2], data, sizeof(data));
        evm_decision_t decision = !strcmp(argv[3], "confirm") ? EVM_CONFIRMED :
                                  !strcmp(argv[3], "reject") ? EVM_REJECTED : EVM_PENDING;
        size = evm_decision_response(data, size, decision, data, sizeof(data));
        if (!size) goto done;
        char hex[EVM_DECISION_CBOR_SIZE * 2 + 1];
        evm_hex(data, size, hex); puts(hex); rc = 0;
    } else if (argc == 3 && !strcmp(argv[1], "review")) {
        uint8_t data[CW_UR_MESSAGE_MAX]; evm_request_t request;
        size_t size = from_hex(argv[2], data, sizeof(data));
        if (!size || evm_parse_request(data, size, &request)) goto done;
        for (unsigned page = 0; page < REVIEW_PAGE_COUNT; ++page) {
            char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1];
            evm_review_lines(&request, page, rows);
            for (unsigned row = 0; row < REVIEW_ROWS; ++row) puts(rows[row]);
        }
        rc = 0;
    } else if (argc == 3 && !strcmp(argv[1], "evm")) {
        uint8_t data[CW_UR_MESSAGE_MAX]; evm_request_t request;
        size_t size = from_hex(argv[2], data, sizeof(data));
        if (!size) goto done;
        const char *error = evm_parse_request(data, size, &request);
        if (error) { puts(error); goto done; }
        char uuid[33], to[41], value[79], fee[79];
        evm_hex(request.request_id, 16, uuid); evm_hex(request.to, 20, to);
        evm_uint256_decimal(request.value, value); evm_uint256_decimal(request.max_total_fee, fee);
        printf("{\"request_id\":\"%s\",\"chain_id\":%" PRIu64 ",\"nonce\":%" PRIu64
            ",\"to\":\"%s\",\"value\":\"%s\",\"gas_limit\":%" PRIu32 ",\"max_total_fee\":\"%s\"}\n",
            uuid, request.chain_id, request.nonce, to, value, request.gas_limit, fee);
        rc = 0;
    }
done:
    cw_ur_reset(); cw_ur_tx_end();
    fprintf(stderr, "arena_peak=%u arena_used=%u\n", (unsigned)cw_ur_memory_peak(), (unsigned)cw_ur_memory_used());
    return cw_ur_memory_used() ? 3 : rc;
}
