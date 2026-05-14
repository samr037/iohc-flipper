#include "phy_rx.h"
#include "../radio/radio_cc1101.h"

#include <furi.h>
#include <furi_hal_resources.h>

#define PHY_QUEUE_CAPACITY 8
#define PHY_POLL_PERIOD_MS 1

struct IohcPhy {
    FuriThread* thread;
    FuriMessageQueue* frame_queue;
    volatile bool running;
    volatile bool sync_event;
    uint32_t frequency_hz;
    uint32_t stat_frames_total;
    uint32_t stat_framing_errors;
};

static void sync_isr(void* ctx) {
    IohcPhy* phy = (IohcPhy*)ctx;
    phy->sync_event = true;
}

static void emit_frame(IohcPhy* phy, const IohcUnframer* u, int8_t rssi) {
    IohcCapturedFrame f = {
        .timestamp_us = furi_get_tick() * 1000,
        .freq_hz = phy->frequency_hz,
        .rssi_dbm = rssi,
        .len = u->frame_pos,
    };
    memcpy(f.bytes, u->frame, u->frame_pos);
    phy->stat_frames_total++;
    // Non-blocking put — drop on overflow rather than stall RX.
    furi_message_queue_put(phy->frame_queue, &f, 0);
}

static int32_t phy_thread(void* ctx) {
    IohcPhy* phy = (IohcPhy*)ctx;
    IohcUnframer unframer;
    iohc_unframer_reset(&unframer);
    bool in_frame = false;
    int8_t frame_rssi = 0;
    uint8_t fifo_buf[64];

    iohc_radio_init();
    iohc_radio_start_rx(phy->frequency_hz);

    furi_hal_gpio_add_int_callback(&gpio_cc1101_g0, sync_isr, phy);
    furi_hal_gpio_init(
        &gpio_cc1101_g0, GpioModeInterruptRise, GpioPullNo, GpioSpeedLow);

    while(phy->running) {
        if(phy->sync_event) {
            phy->sync_event = false;
            if(!in_frame) {
                iohc_unframer_reset(&unframer);
                frame_rssi = iohc_radio_rssi_dbm();
                in_frame = true;
            }
        }

        if(in_frame) {
            uint8_t got = iohc_radio_read_fifo(fifo_buf, sizeof(fifo_buf));
            for(uint8_t i = 0; i < got; i++) {
                IohcUnframerResult r = iohc_unframer_feed_byte(&unframer, fifo_buf[i]);
                if(r == IohcUnframerFrameDone) {
                    emit_frame(phy, &unframer, frame_rssi);
                    in_frame = false;
                    iohc_radio_flush_rx();
                    break;
                } else if(r == IohcUnframerFramingError || r == IohcUnframerOverflow) {
                    phy->stat_framing_errors++;
                    in_frame = false;
                    iohc_radio_flush_rx();
                    break;
                }
            }

            // GDO0 falling edge after sync drop also indicates packet end / lost
            // signal; rely on the unframer's length-driven termination instead.
        }

        furi_delay_ms(PHY_POLL_PERIOD_MS);
    }

    furi_hal_gpio_remove_int_callback(&gpio_cc1101_g0);
    iohc_radio_stop_rx();
    iohc_radio_deinit();
    return 0;
}

IohcPhy* iohc_phy_alloc(void) {
    IohcPhy* phy = malloc(sizeof(IohcPhy));
    memset(phy, 0, sizeof(*phy));
    phy->frame_queue = furi_message_queue_alloc(PHY_QUEUE_CAPACITY, sizeof(IohcCapturedFrame));
    phy->thread = furi_thread_alloc_ex("iohc_phy_rx", 2048, phy_thread, phy);
    return phy;
}

void iohc_phy_free(IohcPhy* phy) {
    if(phy->running) iohc_phy_stop(phy);
    furi_thread_free(phy->thread);
    furi_message_queue_free(phy->frame_queue);
    free(phy);
}

void iohc_phy_start(IohcPhy* phy, uint32_t frequency_hz) {
    phy->frequency_hz = frequency_hz;
    phy->running = true;
    furi_thread_start(phy->thread);
}

void iohc_phy_stop(IohcPhy* phy) {
    phy->running = false;
    furi_thread_join(phy->thread);
}

bool iohc_phy_wait_frame(IohcPhy* phy, IohcCapturedFrame* out, uint32_t timeout_ms) {
    return furi_message_queue_get(phy->frame_queue, out, timeout_ms) == FuriStatusOk;
}

uint32_t iohc_phy_stat_frames_total(IohcPhy* phy) {
    return phy->stat_frames_total;
}

uint32_t iohc_phy_stat_framing_errors(IohcPhy* phy) {
    return phy->stat_framing_errors;
}
