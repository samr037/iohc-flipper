#include "radio_cc1101.h"
#include "radio_cc1101_regs.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <furi_hal_subghz.h>
#include <furi_hal_spi.h>
#include <lib/drivers/cc1101_regs.h>

#define TAG "iohc_radio"

static const FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_subghz;

// Wait for CC1101 chip-ready (CHIP_RDYn = 0, i.e. MISO low). The canonical
// Flipper cc1101 driver does this before every SPI transaction — chip can
// hold MISO high during reset, calibration, or settling. Skipping this is
// why TX corrupts subsequent state.
static void cc1101_wait_ready(void) {
    for(int i = 0; i < 1000; i++) {
        if(!furi_hal_gpio_read(spi->miso)) return;
        furi_delay_us(10);
    }
}

#define SPI_TIMEOUT 100

// CC1101 strobes (datasheet table 38)
#define STROBE_SRES   0x30
#define STROBE_SCAL   0x33
#define STROBE_SRX    0x34
#define STROBE_STX    0x35
#define STROBE_SIDLE  0x36
#define STROBE_SFRX   0x3A
#define STROBE_SFTX   0x3B
#define READ_SINGLE   0x80
#define READ_BURST    0xC0
#define WRITE_BURST   0x40

// MARCSTATE values (datasheet table 32) — read via STATUS_MARCSTATE.
#define MARC_IDLE         0x01
#define MARC_TX           0x13

static void spi_strobe(uint8_t strobe) {
    cc1101_wait_ready();
    furi_hal_spi_bus_tx(spi, &strobe, 1, SPI_TIMEOUT);
}

static void spi_write_reg(uint8_t reg, uint8_t value) {
    cc1101_wait_ready();
    uint8_t buf[2] = {reg, value};
    furi_hal_spi_bus_tx(spi, buf, 2, SPI_TIMEOUT);
}

static uint8_t spi_read_status(uint8_t reg) {
    cc1101_wait_ready();
    // Status registers (0x30..0x3F) require BURST flag, else they're interpreted as strobes.
    uint8_t tx[2] = {reg | READ_BURST, 0x00};
    uint8_t rx[2] = {0, 0};
    furi_hal_spi_bus_trx(spi, tx, rx, 2, SPI_TIMEOUT);
    return rx[1];
}

static uint8_t spi_read_fifo_burst(uint8_t* out, uint8_t n) {
    if(n == 0) return 0;
    if(n > 63) n = 63;
    cc1101_wait_ready();
    uint8_t tx[64];
    uint8_t rx[64];
    memset(tx, 0, n + 1);
    tx[0] = CC1101_FIFO | READ_BURST;  // 0x3F | 0xC0 = 0xFF
    furi_hal_spi_bus_trx(spi, tx, rx, n + 1, SPI_TIMEOUT);
    memcpy(out, &rx[1], n);
    return n;
}

static void write_freq(uint32_t hz) {
    // CC1101 datasheet sec 21: FREQ = round(f * 2^16 / fxosc); fxosc = 26 MHz on Flipper.
    uint64_t f = ((uint64_t)hz << 16);
    uint32_t reg = (uint32_t)((f + 13000000) / 26000000);  // +half for rounding
    spi_write_reg(CC1101_FREQ2, (reg >> 16) & 0xFF);
    spi_write_reg(CC1101_FREQ1, (reg >> 8) & 0xFF);
    spi_write_reg(CC1101_FREQ0, reg & 0xFF);
}

void iohc_radio_init(void) {
    furi_hal_subghz_reset();
}

void iohc_radio_deinit(void) {
    furi_hal_spi_acquire(spi);
    spi_strobe(STROBE_SIDLE);
    furi_hal_spi_release(spi);
    furi_hal_subghz_sleep();
}

static void write_patable(void) {
    cc1101_wait_ready();
    uint8_t tx[9];
    uint8_t rx[9] = {0};
    tx[0] = CC1101_PATABLE | WRITE_BURST;
    memcpy(&tx[1], iohc_cc1101_patable, 8);
    furi_hal_spi_bus_trx(spi, tx, rx, 9, SPI_TIMEOUT);
}

// Poll SNOP strobe until status.STATE matches `state`, or timeout (~10 ms).
// Mirrors canonical cc1101_wait_status_state — required after calibration so
// we don't proceed while the chip is mid-FS-cal.
static bool cc1101_wait_state(uint8_t state) {
    for(int i = 0; i < 1000; i++) {
        cc1101_wait_ready();
        uint8_t tx = 0x3D;  // SNOP strobe (returns chip status on MISO byte 0)
        uint8_t rx = 0xFF;
        furi_hal_spi_bus_trx(spi, &tx, &rx, 1, SPI_TIMEOUT);
        // Status byte: bit 7 = CHIP_RDYn, bits 6:4 = STATE
        if(((rx >> 4) & 0x07) == state) return true;
        furi_delay_us(10);
    }
    return false;
}
#define CC1101_STATE_IDLE 0x00

void iohc_radio_start_rx(uint32_t frequency_hz) {
    // Session 1 — reset + write config registers.
    furi_hal_spi_acquire(spi);
    spi_strobe(STROBE_SRES);
    furi_delay_us(100);  // Datasheet sec 19.1.2: SO goes low after SRES.
    for(size_t i = 0; i < IOHC_CC1101_REG_COUNT; i++) {
        spi_write_reg(iohc_cc1101_regs[i].addr, iohc_cc1101_regs[i].value);
    }
    furi_hal_spi_release(spi);

    // Session 2 — load PA table. Separate SPI session like the canonical
    // Unleashed `furi_hal_subghz_load_custom_preset` does. Doing it within
    // the same session as register writes and SCAL kills RX in our setup.
    furi_hal_spi_acquire(spi);
    write_patable();
    furi_hal_spi_release(spi);

    // Session 3 — frequency + calibrate + wait for IDLE before SRX.
    furi_hal_spi_acquire(spi);
    write_freq(frequency_hz);
    spi_strobe(STROBE_SCAL);
    cc1101_wait_state(CC1101_STATE_IDLE);  // Block until cal completes.
    spi_strobe(STROBE_SFRX);
    spi_strobe(STROBE_SRX);
    furi_hal_spi_release(spi);

    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
}

void iohc_radio_stop_rx(void) {
    furi_hal_spi_acquire(spi);
    spi_strobe(STROBE_SIDLE);
    spi_strobe(STROBE_SFRX);
    furi_hal_spi_release(spi);
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

int8_t iohc_radio_rssi_dbm(void) {
    furi_hal_spi_acquire(spi);
    uint8_t raw = spi_read_status(CC1101_STATUS_RSSI);
    furi_hal_spi_release(spi);
    int16_t v = (raw >= 128) ? ((int16_t)raw - 256) : (int16_t)raw;
    return (int8_t)((v / 2) - 74);  // CC1101 datasheet sec 17.3
}

uint8_t iohc_radio_rx_bytes_available(void) {
    furi_hal_spi_acquire(spi);
    uint8_t n = spi_read_status(CC1101_STATUS_RXBYTES);
    furi_hal_spi_release(spi);
    return n & 0x7F;  // top bit = overflow flag, handled in phy layer
}

uint8_t iohc_radio_read_fifo(uint8_t* buf, uint8_t max_len) {
    furi_hal_spi_acquire(spi);
    uint8_t avail = spi_read_status(CC1101_STATUS_RXBYTES) & 0x7F;
    uint8_t n = avail > max_len ? max_len : avail;
    if(n > 0) spi_read_fifo_burst(buf, n);
    furi_hal_spi_release(spi);
    return n;
}

void iohc_radio_flush_rx(void) {
    furi_hal_spi_acquire(spi);
    spi_strobe(STROBE_SIDLE);
    spi_strobe(STROBE_SFRX);
    spi_strobe(STROBE_SRX);
    furi_hal_spi_release(spi);
}

static void spi_write_fifo_burst(const uint8_t* data, uint8_t n) {
    if(n > 63) n = 63;
    // Write one byte at a time. Burst write of >16 bytes via bus_trx silently
    // truncates in our setup (chip reports only 15 bytes in FIFO regardless
    // of how many we tried to send). Single-byte writes are slower but reliable.
    cc1101_wait_ready();
    uint8_t cmd[2];
    cmd[0] = CC1101_FIFO;  // single-access write, no burst flag
    for(uint8_t i = 0; i < n; i++) {
        cmd[1] = data[i];
        cc1101_wait_ready();
        furi_hal_spi_bus_tx(spi, cmd, 2, SPI_TIMEOUT);
    }
}

bool iohc_radio_tx_frame(const uint8_t* buf, uint8_t len) {
    if(len == 0) return false;
    furi_hal_spi_acquire(spi);

    uint8_t marc_before = spi_read_status(CC1101_STATUS_MARCSTATE) & 0x1F;

    spi_strobe(STROBE_SIDLE);
    spi_strobe(STROBE_SFRX);
    spi_strobe(STROBE_SFTX);

    uint8_t marc_idle = spi_read_status(CC1101_STATUS_MARCSTATE) & 0x1F;

    spi_write_reg(CC1101_PKTLEN, len);
    spi_write_fifo_burst(buf, len);

    uint8_t txbytes_after_load = spi_read_status(CC1101_STATUS_TXBYTES) & 0x7F;

    spi_strobe(STROBE_STX);

    uint8_t marc_after_stx = spi_read_status(CC1101_STATUS_MARCSTATE) & 0x1F;

    furi_delay_ms(25);

    bool ok = false;
    uint8_t marc_final = 0;
    for(int attempt = 0; attempt < 100; attempt++) {
        marc_final = spi_read_status(CC1101_STATUS_MARCSTATE) & 0x1F;
        if(marc_final != MARC_TX) {
            ok = true;
            break;
        }
        furi_delay_us(100);
    }

    uint8_t txbytes_final = spi_read_status(CC1101_STATUS_TXBYTES) & 0x7F;

    FURI_LOG_I(TAG, "TX len=%u  marc: before=0x%02X idle=0x%02X after_stx=0x%02X final=0x%02X  txbytes: loaded=%u left=%u",
        (unsigned)len, marc_before, marc_idle, marc_after_stx, marc_final,
        (unsigned)txbytes_after_load, (unsigned)txbytes_final);

    // Force back to a known-good state regardless of how TX ended:
    // SIDLE recovers from TXFIFO_UNDERFLOW; both flushes clear stale data;
    // then re-enter RX with the original PKTLEN.
    spi_strobe(STROBE_SIDLE);
    spi_strobe(STROBE_SFTX);
    spi_strobe(STROBE_SFRX);
    spi_write_reg(CC1101_PKTLEN, 0xFF);
    spi_strobe(STROBE_SRX);
    furi_delay_us(800);  // wait for IDLE→RX settle
    uint8_t marc_post_rx = spi_read_status(CC1101_STATUS_MARCSTATE) & 0x1F;

    furi_hal_spi_release(spi);

    FURI_LOG_I(TAG, "post-recovery marc=0x%02X (expect 0x0D=RX)", marc_post_rx);
    return ok;
}
