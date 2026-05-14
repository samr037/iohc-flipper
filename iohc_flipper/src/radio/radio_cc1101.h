#pragma once

#include <stdint.h>
#include <stdbool.h>

void iohc_radio_init(void);
void iohc_radio_deinit(void);
void iohc_radio_start_rx(uint32_t frequency_hz);
void iohc_radio_stop_rx(void);
int8_t iohc_radio_rssi_dbm(void);
uint8_t iohc_radio_rx_bytes_available(void);
uint8_t iohc_radio_read_fifo(uint8_t* buf, uint8_t max_len);
void iohc_radio_flush_rx(void);

// TX one fixed-length frame from FIFO. Blocking; returns when chip is back
// in RX mode. `buf` is the bit-packed FIFO content per frame_build.h —
// must start with the 0x99 residue byte and end on a byte boundary.
// `len` is the number of FIFO bytes (typically 24–42).
// Returns false on TX timeout.
bool iohc_radio_tx_frame(const uint8_t* buf, uint8_t len);
