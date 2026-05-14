#include "log.h"
#include "../iohc/frame_parse.h"

#include <storage/storage.h>
#include <furi.h>
#include <stdio.h>

#define IOHC_LOG_DIR EXT_PATH("apps_data/iohc_flipper")

struct IohcLog {
    Storage* storage;
    File* csv;
    File* bin;
};

static void open_session_files(IohcLog* log) {
    storage_common_mkdir(log->storage, IOHC_LOG_DIR);
    uint32_t t = furi_get_tick();
    char csv_path[128];
    char bin_path[128];
    snprintf(csv_path, sizeof(csv_path), "%s/frames_%lu.csv", IOHC_LOG_DIR, t);
    snprintf(bin_path, sizeof(bin_path), "%s/frames_%lu.bin", IOHC_LOG_DIR, t);
    storage_file_open(log->csv, csv_path, FSAM_WRITE, FSOM_CREATE_NEW);
    storage_file_open(log->bin, bin_path, FSAM_WRITE, FSOM_CREATE_NEW);
    const char* hdr =
        "timestamp_us,freq_hz,rssi_dbm,len,src,dst,cmdid,oneway,crc_ok,hex\n";
    storage_file_write(log->csv, hdr, strlen(hdr));
}

IohcLog* iohc_log_alloc(void) {
    IohcLog* log = malloc(sizeof(IohcLog));
    log->storage = furi_record_open(RECORD_STORAGE);
    log->csv = storage_file_alloc(log->storage);
    log->bin = storage_file_alloc(log->storage);
    open_session_files(log);
    return log;
}

void iohc_log_free(IohcLog* log) {
    storage_file_close(log->csv);
    storage_file_close(log->bin);
    storage_file_free(log->csv);
    storage_file_free(log->bin);
    furi_record_close(RECORD_STORAGE);
    free(log);
}

static char hex_char(uint8_t nib) {
    return (char)((nib < 10) ? ('0' + nib) : ('A' + nib - 10));
}

static void bytes_to_hex(const uint8_t* b, uint8_t n, char* out) {
    for(uint8_t i = 0; i < n; i++) {
        out[i * 2] = hex_char(b[i] >> 4);
        out[i * 2 + 1] = hex_char(b[i] & 0xF);
    }
    out[n * 2] = 0;
}

void iohc_log_write(IohcLog* log, const IohcCapturedFrame* f) {
    char hex[IOHC_FRAME_MAX_LEN * 2 + 1];
    bytes_to_hex(f->bytes, f->len, hex);

    IohcParsedFrame p;
    bool ok = iohc_frame_parse(f->bytes, f->len, &p);

    char src[7] = "------";
    char dst[7] = "------";
    int cmd = -1, oneway = -1, crc = -1;
    if(ok) {
        bytes_to_hex(p.src, 3, src);
        bytes_to_hex(p.dst, 3, dst);
        cmd = p.cmdid;
        oneway = p.one_way ? 1 : 0;
        crc = p.crc_ok ? 1 : 0;
    }

    char line[300];
    int n = snprintf(line, sizeof(line),
        "%lu,%lu,%d,%u,%s,%s,%d,%d,%d,%s\n",
        (unsigned long)f->timestamp_us, (unsigned long)f->freq_hz,
        (int)f->rssi_dbm, (unsigned)f->len,
        src, dst, cmd, oneway, crc, hex);
    storage_file_write(log->csv, line, (size_t)n);
    storage_file_write(log->bin, f->bytes, f->len);
}
