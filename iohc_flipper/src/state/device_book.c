#include "device_book.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <storage/storage.h>
#include <stdio.h>

#define BIN_PATH      EXT_PATH("apps_data/iohc_flipper/devices.bin")
#define BIN_BAK_PATH  EXT_PATH("apps_data/iohc_flipper/devices.bak.bin")
#define LEGACY_PATH   EXT_PATH("apps_data/iohc_flipper/devices.txt")
#define BIN_MAGIC     0x49484344  // "IHCD"
#define BIN_VERSION   3
#define BIN_VERSION_V1 1
#define BIN_VERSION_V2 2

// Default vendor for v1 → v2 migration: existing entries pre-Phase-6a were
// all Somfy (the only vendor the FAP could TX at the time).
#define MIGRATE_DEFAULT_VENDOR 0x43

struct IohcDeviceBook {
    IohcDevice items[IOHC_DEVICE_BOOK_MAX];
    uint8_t count;
};

IohcDeviceBook* iohc_device_book_alloc(void) {
    IohcDeviceBook* b = malloc(sizeof(IohcDeviceBook));
    memset(b, 0, sizeof(*b));
    return b;
}

void iohc_device_book_free(IohcDeviceBook* b) {
    free(b);
}

uint8_t iohc_device_book_count(const IohcDeviceBook* b) {
    return b->count;
}

const IohcDevice* iohc_device_book_get(const IohcDeviceBook* b, uint8_t index) {
    return index < b->count ? &b->items[index] : NULL;
}

IohcDevice* iohc_device_book_get_mut(IohcDeviceBook* b, uint8_t index) {
    return index < b->count ? &b->items[index] : NULL;
}

static void generate_fresh_identity(IohcIdentity* id) {
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

uint8_t iohc_man_id_default_for_vendor(uint8_t vendor) {
    // Observed correspondences. Used only when we haven't sniffed a cmd 0x30
    // frame from the remote (rare in normal use). Unknown vendors → 0x02
    // (Somfy default — the more permissive of the two we've tested).
    switch(vendor) {
        case 0x43: return 0x02;  // Somfy
        case 0x61: return 0x01;  // Velux
        default:   return 0x02;
    }
}

int iohc_device_book_add(
    IohcDeviceBook* b, const char* name, const uint8_t motor_addr[3],
    uint8_t vendor, uint8_t man_id) {
    if(b->count >= IOHC_DEVICE_BOOK_MAX) return -1;
    IohcDevice* d = &b->items[b->count];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, IOHC_DEVICE_NAME_MAX);
    d->name[IOHC_DEVICE_NAME_MAX] = '\0';
    memcpy(d->motor_addr, motor_addr, 3);
    d->vendor = vendor;
    d->man_id = man_id;
    generate_fresh_identity(&d->identity);
    int idx = b->count;
    b->count++;
    return idx;
}

void iohc_device_book_rename(IohcDeviceBook* b, uint8_t index, const char* new_name) {
    if(index >= b->count) return;
    strncpy(b->items[index].name, new_name, IOHC_DEVICE_NAME_MAX);
    b->items[index].name[IOHC_DEVICE_NAME_MAX] = '\0';
}

void iohc_device_book_remove(IohcDeviceBook* b, uint8_t index) {
    if(index >= b->count) return;
    for(uint8_t i = index; i + 1 < b->count; i++) {
        b->items[i] = b->items[i + 1];
    }
    b->count--;
}

bool iohc_device_book_has_addr(const IohcDeviceBook* b, const uint8_t addr[3]) {
    for(uint8_t i = 0; i < b->count; i++) {
        if(memcmp(b->items[i].motor_addr, addr, 3) == 0) return true;
    }
    return false;
}

uint16_t iohc_device_book_next_seq(IohcDeviceBook* b, uint8_t index) {
    if(index >= b->count) return 0;
    b->items[index].identity.seq++;
    uint16_t v = b->items[index].identity.seq;
    iohc_device_book_save(b);
    return v;
}

// On-disk format (little-endian):
//   [0..3]   magic = 0x49484344 ('IHCD')
//   [4]      version
//   [5]      count (entries)
//   [6..]    count × entry (size depends on version)
//
// v1 entry (49 bytes): { name[25], motor_addr[3], src[3], key[16], seq_be[2] }
// v2 entry (50 bytes): v1 + vendor[1]
// v3 entry (51 bytes): v2 + man_id[1] → vendor and man_id are stored as
//                      independent raw wire bytes (per rspaargaren's design).
#define ENTRY_SIZE_V1 49
#define ENTRY_SIZE_V2 50
#define ENTRY_SIZE    51  // current version
#define HEADER_SIZE   6

static void serialize_entry(const IohcDevice* d, uint8_t buf[ENTRY_SIZE]) {
    memcpy(&buf[0], d->name, IOHC_DEVICE_NAME_MAX + 1);
    memcpy(&buf[25], d->motor_addr, 3);
    memcpy(&buf[28], d->identity.src_addr, 3);
    memcpy(&buf[31], d->identity.install_key, 16);
    buf[47] = (uint8_t)((d->identity.seq >> 8) & 0xFF);
    buf[48] = (uint8_t)(d->identity.seq & 0xFF);
    buf[49] = d->vendor;
    buf[50] = d->man_id;
}

// Common deserialization for the bytes shared by all versions.
static void deserialize_entry_v1(IohcDevice* d, const uint8_t buf[ENTRY_SIZE_V1]) {
    memcpy(d->name, &buf[0], IOHC_DEVICE_NAME_MAX + 1);
    d->name[IOHC_DEVICE_NAME_MAX] = '\0';
    memcpy(d->motor_addr, &buf[25], 3);
    memcpy(d->identity.src_addr, &buf[28], 3);
    memcpy(d->identity.install_key, &buf[31], 16);
    d->identity.seq = (uint16_t)(buf[47] << 8) | buf[48];
    d->vendor = MIGRATE_DEFAULT_VENDOR;
    d->man_id = iohc_man_id_default_for_vendor(MIGRATE_DEFAULT_VENDOR);
}

static void deserialize_entry_v2(IohcDevice* d, const uint8_t buf[ENTRY_SIZE_V2]) {
    deserialize_entry_v1(d, buf);
    d->vendor = buf[49];
    d->man_id = iohc_man_id_default_for_vendor(d->vendor);
}

static void deserialize_entry(IohcDevice* d, const uint8_t buf[ENTRY_SIZE]) {
    deserialize_entry_v2(d, buf);
    d->man_id = buf[50];
}

static bool write_book_file(Storage* storage, const char* path, const IohcDeviceBook* b) {
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        uint8_t hdr[HEADER_SIZE] = {
            BIN_MAGIC & 0xFF, (BIN_MAGIC >> 8) & 0xFF,
            (BIN_MAGIC >> 16) & 0xFF, (BIN_MAGIC >> 24) & 0xFF,
            BIN_VERSION, b->count,
        };
        storage_file_write(f, hdr, HEADER_SIZE);
        uint8_t entry[ENTRY_SIZE];
        for(uint8_t i = 0; i < b->count; i++) {
            serialize_entry(&b->items[i], entry);
            storage_file_write(f, entry, ENTRY_SIZE);
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}

bool iohc_device_book_save(const IohcDeviceBook* b) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, EXT_PATH("apps_data/iohc_flipper"));
    // Primary first, then mirror — see identity.c for rationale.
    bool ok_main = write_book_file(storage, BIN_PATH, b);
    write_book_file(storage, BIN_BAK_PATH, b);
    furi_record_close(RECORD_STORAGE);
    return ok_main;
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Migrate legacy text format (name,XXXXXX\n per line). Generates fresh
// identities for every entry — old global-identity pairings are invalidated.
static bool try_migrate_from_txt(IohcDeviceBook* b) {
    b->count = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    bool found = false;
    if(storage_file_open(f, LEGACY_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        found = true;
        char buf[1024];
        uint16_t n = storage_file_read(f, buf, sizeof(buf) - 1);
        buf[n] = '\0';
        char* line = buf;
        while(line && *line && b->count < IOHC_DEVICE_BOOK_MAX) {
            char* eol = strchr(line, '\n');
            if(eol) *eol = '\0';
            char* comma = strrchr(line, ',');
            if(comma && strlen(comma + 1) >= 6) {
                *comma = '\0';
                uint8_t addr[3];
                bool parse_ok = true;
                for(int i = 0; i < 3 && parse_ok; i++) {
                    int hi = hex_nibble(comma[1 + i * 2]);
                    int lo = hex_nibble(comma[2 + i * 2]);
                    if(hi < 0 || lo < 0) parse_ok = false;
                    else addr[i] = (uint8_t)((hi << 4) | lo);
                }
                if(parse_ok && line[0] != '\0') {
                    iohc_device_book_add(b, line, addr, MIGRATE_DEFAULT_VENDOR,
                        iohc_man_id_default_for_vendor(MIGRATE_DEFAULT_VENDOR));
                }
            }
            line = eol ? eol + 1 : NULL;
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return found;
}

bool iohc_device_book_load(IohcDeviceBook* b) {
    b->count = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    bool loaded = false;
    bool needs_resave = false;  // set when we migrated to a newer format
    if(storage_file_open(f, BIN_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t hdr[HEADER_SIZE];
        if(storage_file_read(f, hdr, HEADER_SIZE) == HEADER_SIZE) {
            uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8)
                           | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
            uint8_t version = hdr[4];
            uint8_t count = hdr[5];
            if(count > IOHC_DEVICE_BOOK_MAX) count = IOHC_DEVICE_BOOK_MAX;

            if(magic == BIN_MAGIC && version == BIN_VERSION) {
                uint8_t entry[ENTRY_SIZE];
                for(uint8_t i = 0; i < count; i++) {
                    if(storage_file_read(f, entry, ENTRY_SIZE) != ENTRY_SIZE) break;
                    deserialize_entry(&b->items[b->count], entry);
                    b->count++;
                }
                loaded = true;
            } else if(magic == BIN_MAGIC && version == BIN_VERSION_V2) {
                // v2 → v3: read v2 entries, derive man_id from vendor.
                uint8_t entry[ENTRY_SIZE_V2];
                for(uint8_t i = 0; i < count; i++) {
                    if(storage_file_read(f, entry, ENTRY_SIZE_V2) != ENTRY_SIZE_V2) break;
                    deserialize_entry_v2(&b->items[b->count], entry);
                    b->count++;
                }
                loaded = true;
                needs_resave = true;
            } else if(magic == BIN_MAGIC && version == BIN_VERSION_V1) {
                // v1 → v3: read v1 entries, default vendor and man_id to Somfy.
                uint8_t entry[ENTRY_SIZE_V1];
                for(uint8_t i = 0; i < count; i++) {
                    if(storage_file_read(f, entry, ENTRY_SIZE_V1) != ENTRY_SIZE_V1) break;
                    deserialize_entry_v1(&b->items[b->count], entry);
                    b->count++;
                }
                loaded = true;
                needs_resave = true;
            }
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);

    if(!loaded) {
        // Try legacy txt → migrate.
        if(try_migrate_from_txt(b) && b->count > 0) {
            iohc_device_book_save(b);  // persist as new binary format
            loaded = true;
        }
    } else if(needs_resave) {
        // Re-write file in current layout so subsequent loads take the fast path.
        iohc_device_book_save(b);
    }
    return loaded;
}

static int hex_dump(char* dst, const uint8_t* src, size_t n) {
    static const char H[] = "0123456789ABCDEF";
    for(size_t i = 0; i < n; i++) {
        dst[2 * i]     = H[(src[i] >> 4) & 0xF];
        dst[2 * i + 1] = H[src[i]        & 0xF];
    }
    dst[2 * n] = '\0';
    return (int)(2 * n);
}

bool iohc_device_book_export_json(
    const IohcDeviceBook* b, const IohcIdentity* global, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, EXT_PATH("apps_data/iohc_flipper"));
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        char line[256];
        char src_hex[7], key_hex[33], motor_hex[7];

        hex_dump(src_hex, global->src_addr, 3);
        hex_dump(key_hex, global->install_key, 16);

        int n = snprintf(line, sizeof(line),
            "{\n  \"format\": \"iohc_flipper_export\",\n  \"version\": 1,\n");
        storage_file_write(f, line, n);

        n = snprintf(line, sizeof(line),
            "  \"global_identity\": { \"src\": \"%s\", \"install_key\": \"%s\", \"seq\": %u },\n",
            src_hex, key_hex, (unsigned)global->seq);
        storage_file_write(f, line, n);

        n = snprintf(line, sizeof(line), "  \"devices\": [\n");
        storage_file_write(f, line, n);

        for(uint8_t i = 0; i < b->count; i++) {
            const IohcDevice* d = &b->items[i];
            hex_dump(motor_hex, d->motor_addr, 3);
            hex_dump(src_hex, d->identity.src_addr, 3);
            hex_dump(key_hex, d->identity.install_key, 16);
            n = snprintf(line, sizeof(line),
                "    { \"name\": \"%s\", \"motor_addr\": \"%s\", "
                "\"vendor\": \"0x%02X\", \"man_id\": \"0x%02X\", "
                "\"src\": \"%s\", \"install_key\": \"%s\", \"seq\": %u }%s\n",
                d->name, motor_hex,
                (unsigned)d->vendor, (unsigned)d->man_id,
                src_hex, key_hex,
                (unsigned)d->identity.seq,
                (i + 1 < b->count) ? "," : "");
            storage_file_write(f, line, n);
        }

        n = snprintf(line, sizeof(line), "  ]\n}\n");
        storage_file_write(f, line, n);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
