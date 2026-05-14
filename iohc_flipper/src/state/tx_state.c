#include "tx_state.h"
#include <furi.h>

struct IohcTxState {
    FuriMutex* m;
    uint16_t last_seq;  // highest seq we've seen or transmitted
};

IohcTxState* iohc_tx_state_alloc(void) {
    IohcTxState* s = malloc(sizeof(IohcTxState));
    s->m = furi_mutex_alloc(FuriMutexTypeNormal);
    s->last_seq = 0;
    return s;
}

void iohc_tx_state_free(IohcTxState* s) {
    furi_mutex_free(s->m);
    free(s);
}

void iohc_tx_state_observe_seq(IohcTxState* s, uint16_t observed) {
    furi_mutex_acquire(s->m, FuriWaitForever);
    if(observed > s->last_seq) s->last_seq = observed;
    furi_mutex_release(s->m);
}

uint16_t iohc_tx_state_next_seq(IohcTxState* s) {
    furi_mutex_acquire(s->m, FuriWaitForever);
    s->last_seq++;
    uint16_t v = s->last_seq;
    furi_mutex_release(s->m);
    return v;
}
