#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef struct IohcCaptureView IohcCaptureView;

// Fires when user presses OK on a captured remote.
// `addr` is 3 bytes — a motor address label (last non-broadcast dst we saw
// from this src, or src itself if none). Valid for callback duration.
// `vendor` is the payload[1] byte observed in a button frame (cmd 0x00)
// from this src — passed through verbatim to TX later.
typedef void (*IohcCaptureOnSave)(const uint8_t* addr, uint8_t vendor, void* ctx);

IohcCaptureView* iohc_capture_view_alloc(void);
void iohc_capture_view_free(IohcCaptureView* v);
View* iohc_capture_view_get_view(IohcCaptureView* v);

void iohc_capture_view_set_on_save(IohcCaptureView* v, IohcCaptureOnSave cb, void* ctx);

// Reset on entering — clears any prior capture.
void iohc_capture_view_reset(IohcCaptureView* v);

// Called from the consumer thread when a new frame is parsed. Capture
// view indexes by source address — one row per unique remote, regardless
// of how many type-code dsts that remote emits to.
//
// `cmd` is the iohc frame command (we only trust payload[1] as vendor when
// it's a button frame cmd=0x00; for cmd 0x2E/0x30 payload[1] is a counter).
// `payload_byte_1` is payload[1] if payload_len>=2 else 0.
void iohc_capture_view_observe(
    IohcCaptureView* v, const uint8_t src[3], const uint8_t dst[3],
    int8_t rssi, uint8_t cmd, uint8_t payload_byte_1);
