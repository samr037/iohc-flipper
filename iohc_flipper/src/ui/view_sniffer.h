#pragma once

#include <gui/view.h>
#include "../phy/phy_rx.h"
#include "../iohc/frame_parse.h"

typedef struct IohcSniffView IohcSniffView;
typedef void (*IohcSniffOnLockToggle)(void* ctx);
typedef void (*IohcSniffOnOpenTx)(void* ctx);

IohcSniffView* iohc_sniff_view_alloc(void);
void iohc_sniff_view_free(IohcSniffView* v);
View* iohc_sniff_view_get_view(IohcSniffView* v);

void iohc_sniff_view_set_lock_toggle(
    IohcSniffView* v, IohcSniffOnLockToggle cb, void* ctx);
void iohc_sniff_view_set_open_tx(
    IohcSniffView* v, IohcSniffOnOpenTx cb, void* ctx);

// Pass parsed=NULL on tick updates (no new frame); the last shown parsed
// values stay on screen.
void iohc_sniff_view_update(
    IohcSniffView* v,
    uint32_t frame_count,
    uint32_t errors,
    const IohcCapturedFrame* last,
    const IohcParsedFrame* parsed,
    bool lock_active,
    const uint8_t lock_src[3]);
