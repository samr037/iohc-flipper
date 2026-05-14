#pragma once

// CC1101 register set for io-homecontrol RX on the Flipper Zero.
// Sources: SUMMARY.md §3.3 (Velocet, rspaargaren, rtl_433) and TIMING_BUDGET.md.
// The 16-bit sync 0x57FD matches preamble-tail + UART-wrapped 0xFF — see
// rtl_433/src/devices/somfy_iohc.c:96 and TIMING_BUDGET.md "Encoding insight".

#include <lib/drivers/cc1101_regs.h>

#define IOHC_FREQ_HZ_CH2 868950000UL  // default landing channel (rspaargaren/include/board-config.h:134)
#define IOHC_FREQ_HZ_CH1 868250000UL
#define IOHC_FREQ_HZ_CH3 869850000UL

// PA table — index 0 used for TX (FREND0.PA_POWER = 0). 0xC0 → +10 dBm at
// 868 MHz per CC1101 datasheet table 43, matches typical Somfy/Velux remote
// output power. Index 1+ unused.
static const uint8_t iohc_cc1101_patable[8] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Register table — order matters where it interacts with chip state, but
// for a clean SRES->config sequence the order is just for readability.
//
// PKTCTRL0 = 0x00: normal FIFO mode, no whitening, no CRC autoflush, fixed length.
// PKTCTRL1 = 0x00: no addr filter, no append-status, PQT_VAL=0 → sync detect armed without preamble gate.
// MDMCFG2 = 0x06: 2-FSK, no Manchester, SYNC_MODE=110 (16/16 + carrier sense).
// IOCFG0 = 0x06: GDO0 asserts on sync-word received, de-asserts at end of packet — our only ISR.
// IOCFG2 = 0x2E: GDO2 disabled (not wired on Flipper Zero anyway).
//
// SYNC bytes carry the trick: 0x57 = preamble tail (0101) + first 4 bits of wrapped 0xFF (0111).
// 0xFD = next 8 bits of wrapped 0xFF (11111101).
// Together the chip locks on the iohc preamble→sync transition.
typedef struct {
    uint8_t addr;
    uint8_t value;
} IohcCc1101Reg;

static const IohcCc1101Reg iohc_cc1101_regs[] = {
    {CC1101_IOCFG2, 0x2E},   // GDO2 disabled (high-Z, unused on FZ)
    {CC1101_IOCFG0, 0x06},   // GDO0 = SYNC_WORD_SENT_OR_RECEIVED
    {CC1101_FIFOTHR, 0x07},  // FIFOTHR 7 → RX FIFO above 32 bytes triggers (we poll, GDO0 unused for this)
    {CC1101_SYNC1, 0x57},    // SUMMARY.md §3.3 + UART-encoding derivation
    {CC1101_SYNC0, 0xFD},
    {CC1101_PKTLEN, 0xFF},
    {CC1101_PKTCTRL1, 0x00}, // no addr filter, no status append, PQT_VAL=0
    {CC1101_PKTCTRL0, 0x00}, // FIFO mode, no whitening, no HW CRC, fixed length
    {CC1101_ADDR, 0x00},
    {CC1101_CHANNR, 0x00},
    {CC1101_FSCTRL1, 0x06},
    {CC1101_FSCTRL0, 0x00},
    // FREQ2/1/0 written by cc1101_set_frequency() — leave defaults here
    {CC1101_MDMCFG4, 0x6A},  // RX BW 270 kHz | DRATE_E=0xA
    {CC1101_MDMCFG3, 0x83},  // DRATE_M=131 → 38383.5 bps
    {CC1101_MDMCFG2, 0x06},  // 2-FSK, no Manchester, 16/16 sync + CS
    {CC1101_MDMCFG1, 0x22},  // FEC off, NUM_PREAMBLE=4 (TX-only, harmless on RX), CHANSPC_E=2
    {CC1101_MDMCFG0, 0xF8},
    {CC1101_DEVIATN, 0x34},  // 19.04 kHz dev
    {CC1101_MCSM2, 0x07},    // RX_TIME=7, no timeout
    {CC1101_MCSM1, 0x00},    // CCA_MODE=00 (always TX, ignore RSSI), TXOFF=IDLE, RXOFF=IDLE
                             // CCA=11 was blocking STX when iohc channel had ambient traffic.
    {CC1101_MCSM0, 0x18},    // cal on IDLE→RX/TX, PO_TIMEOUT ~150 µs
    {CC1101_FOCCFG, 0x16},
    {CC1101_BSCFG, 0x6C},
    {CC1101_AGCCTRL2, 0x43},
    {CC1101_AGCCTRL1, 0x40},
    {CC1101_AGCCTRL0, 0x91},
    {CC1101_FREND1, 0x56},
    {CC1101_FREND0, 0x10},
    {CC1101_FSCAL3, 0xE9},
    {CC1101_FSCAL2, 0x2A},
    {CC1101_FSCAL1, 0x00},
    {CC1101_FSCAL0, 0x1F},
    {CC1101_TEST2, 0x81},
    {CC1101_TEST1, 0x35},
    {CC1101_TEST0, 0x09},
};

#define IOHC_CC1101_REG_COUNT (sizeof(iohc_cc1101_regs) / sizeof(iohc_cc1101_regs[0]))
