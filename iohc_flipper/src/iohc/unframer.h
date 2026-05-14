#pragma once

#include <stdint.h>
#include <stdbool.h>

#define IOHC_FRAME_MAX_LEN 32  // SUMMARY.md §1.2

// After CC1101 sync detect on 0x57FD, the chip has already consumed the
// 0xFF wrap and the start + first data bit of the 0x33 wrap. The remaining
// 8 bits of the wrapped 0x33 land in the very first FIFO byte. The unframer
// state machine "primes" itself by discarding those 8 bits, then resumes
// 10-bit-per-iohc-byte UART unwrapping. See TIMING_BUDGET.md "Encoding insight".

typedef enum {
    IohcUnframerOk,           // byte appended successfully
    IohcUnframerNeedBits,     // not enough bits yet, no byte produced this call
    IohcUnframerFrameDone,    // length-byte-driven end of frame reached
    IohcUnframerFramingError, // start/stop bit mismatch — frame aborted
    IohcUnframerOverflow,     // frame longer than IOHC_FRAME_MAX_LEN
} IohcUnframerResult;

typedef struct {
    uint32_t bit_buffer;      // MSB-first staging area
    uint8_t bit_count;
    uint8_t skip_bits;        // tail of wrapped 0x33 to discard after sync (8)
    uint8_t expected_len;     // 0 until Ctrl B1 parsed; then total iohc bytes incl CRC
    uint8_t frame_pos;
    uint8_t frame[IOHC_FRAME_MAX_LEN];
} IohcUnframer;

void iohc_unframer_reset(IohcUnframer* u);
IohcUnframerResult iohc_unframer_feed_byte(IohcUnframer* u, uint8_t fifo_byte);
