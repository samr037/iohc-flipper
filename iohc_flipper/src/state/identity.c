#include "identity.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <storage/storage.h>

#define IDENTITY_PATH     EXT_PATH("apps_data/iohc_flipper/identity.bin")
#define IDENTITY_BAK_PATH EXT_PATH("apps_data/iohc_flipper/identity.bak.bin")
#define IDENTITY_SIZE 21

static void generate_random_identity(IohcIdentity* id) {
    // Loop until we pick a non-reserved source address.
    while(true) {
        furi_hal_random_fill_buf(id->src_addr, 3);
        const bool reserved =
            (id->src_addr[0] == 0x00 && id->src_addr[1] == 0x00 && id->src_addr[2] == 0x3F) ||
            (id->src_addr[0] == 0xFF && id->src_addr[1] == 0xFF &&
             (id->src_addr[2] == 0xFE || id->src_addr[2] == 0xFF));
        if(!reserved) break;
    }
    furi_hal_random_fill_buf(id->install_key, 16);
    id->seq = 0;
}

static void serialize(const IohcIdentity* id, uint8_t buf[IDENTITY_SIZE]) {
    memcpy(&buf[0], id->src_addr, 3);
    memcpy(&buf[3], id->install_key, 16);
    buf[19] = (uint8_t)((id->seq >> 8) & 0xFF);
    buf[20] = (uint8_t)(id->seq & 0xFF);
}

static void deserialize(IohcIdentity* id, const uint8_t buf[IDENTITY_SIZE]) {
    memcpy(id->src_addr, &buf[0], 3);
    memcpy(id->install_key, &buf[3], 16);
    id->seq = (uint16_t)(buf[19] << 8) | buf[20];
}

static bool write_identity_file(Storage* storage, const char* path, const uint8_t* buf) {
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        ok = storage_file_write(f, buf, IDENTITY_SIZE) == IDENTITY_SIZE;
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}

bool iohc_identity_save(const IohcIdentity* id) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, EXT_PATH("apps_data/iohc_flipper"));
    uint8_t buf[IDENTITY_SIZE];
    serialize(id, buf);
    // Primary first, then mirror. If primary fails we still try the mirror —
    // a stale primary + fresh mirror is recoverable; total loss is not.
    bool ok_main = write_identity_file(storage, IDENTITY_PATH, buf);
    write_identity_file(storage, IDENTITY_BAK_PATH, buf);
    furi_record_close(RECORD_STORAGE);
    return ok_main;
}

bool iohc_identity_load_or_generate(IohcIdentity* out) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    bool loaded = false;
    if(storage_file_open(f, IDENTITY_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t buf[IDENTITY_SIZE];
        if(storage_file_read(f, buf, IDENTITY_SIZE) == IDENTITY_SIZE) {
            deserialize(out, buf);
            loaded = true;
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    if(loaded) return true;

    generate_random_identity(out);
    return iohc_identity_save(out);
}

uint16_t iohc_identity_next_seq(IohcIdentity* id) {
    id->seq++;
    uint16_t v = id->seq;
    iohc_identity_save(id);
    return v;
}
