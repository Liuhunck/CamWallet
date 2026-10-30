#ifndef CAMWALLET_EVM_REVIEW_H
#define CAMWALLET_EVM_REVIEW_H
#include "evm_request.h"
#include "evm_decision.h"
#define REVIEW_PAGE_COUNT 5u
#define REVIEW_ROWS 12u
#define REVIEW_COLUMNS 20u
typedef enum { REVIEW_NO_BUTTON, REVIEW_BACK, REVIEW_REJECT, REVIEW_NEXT } review_button_t;
typedef struct {
    unsigned page;
    evm_decision_t decision;
    unsigned touch_state;
    uint64_t since;
    bool timing;
    review_button_t button;
} evm_review_t;
void evm_review_init(evm_review_t *review);
/* Release-triggered, debounced taps only. Invalid I/O cancels the gesture.
 * Returns true only when the page or decision changes. */
bool evm_review_touch(evm_review_t *review, bool valid, uint8_t fingers,
                      uint16_t x, uint16_t y, uint64_t now_ms);
review_button_t evm_review_hit(unsigned page, uint16_t x, uint16_t y);
void evm_review_lines(const evm_request_t *request, unsigned page,
                      char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1]);
bool evm_review_render(uint8_t *frame, size_t size, const evm_request_t *request,
                       const evm_review_t *review);
#endif
