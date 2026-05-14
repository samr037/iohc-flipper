#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi.h>
#include "../iohc/unframer.h"

typedef struct {
    uint32_t timestamp_us;
    uint32_t freq_hz;
    int8_t rssi_dbm;
    uint8_t len;
    uint8_t bytes[IOHC_FRAME_MAX_LEN];
} IohcCapturedFrame;

// Bounded queue; oldest dropped if full to avoid blocking the radio task.
typedef struct IohcPhy IohcPhy;

IohcPhy* iohc_phy_alloc(void);
void iohc_phy_free(IohcPhy* phy);
void iohc_phy_start(IohcPhy* phy, uint32_t frequency_hz);
void iohc_phy_stop(IohcPhy* phy);

// Blocking up to `timeout_ms`. Returns true if a frame was dequeued.
bool iohc_phy_wait_frame(IohcPhy* phy, IohcCapturedFrame* out, uint32_t timeout_ms);

uint32_t iohc_phy_stat_frames_total(IohcPhy* phy);
uint32_t iohc_phy_stat_framing_errors(IohcPhy* phy);
