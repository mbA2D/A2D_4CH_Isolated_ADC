#include "A2D_SettingsStore.h"
#include "A2D_Flash.h"
#include <stddef.h>
#include <math.h>
#include <string.h>
static const uint32_t Magic = 0x33444341UL, LegacyMagic = 0x32444341UL, Commit = 0xc0dec0deUL;
static uint32_t crc32(const void *data, size_t size) {
    uint32_t crc = 0xffffffffUL;
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1)));
    }
    return ~crc;
}
static uint32_t checksum(const A2D_SettingsRecord *record) { return crc32(record, offsetof(A2D_SettingsRecord, crc)); }
static bool valid(const A2D_SettingsRecord *record) {
    if (record->magic != Magic || record->commit != Commit || record->address > 31 || record->crc != checksum(record) ||
        (record->serial > 9999 && record->serial != A2D_UNASSIGNED_SERIAL)) return false;
    for (uint8_t i = 0; i < 4; ++i)
        if (!isfinite(record->calibration[i].gain) || record->calibration[i].gain == 0 || !isfinite(record->calibration[i].offset)) return false;
    return true;
}
static bool save(A2D_SettingsStore *store, A2D_SettingsRecord record, bool force) {
    for (uint8_t i = 0; i < 4; ++i) if (!a2d_calibration_valid(record.calibration[i])) return false;
    if (!force && store->active >= 0 && record.address == store->saved.address &&
        record.nextRequest == store->saved.nextRequest && record.serial == store->saved.serial &&
        !memcmp(record.calibration, store->saved.calibration, sizeof(record.calibration))) return true;
    record.magic = Magic;
    record.sequence = store->active < 0 ? 0 : store->saved.sequence + 1;
    record.commit = Commit;
    record.crc = checksum(&record);
    if (!valid(&record)) return false;
    const uint8_t target = store->active == 0 ? 1 : 0;
    if (!a2d_flash_write_page(target, &record, sizeof(record))) return false;
    A2D_SettingsRecord verify;
    a2d_flash_read_page(target, &verify, sizeof(verify));
    if (!valid(&verify) || memcmp(&verify, &record, sizeof(record))) return false;
    store->active = (int8_t)target; store->saved = record;
    return true;
}
void a2d_settings_init(A2D_SettingsStore *store) {
    memset(store, 0, sizeof(*store));
    store->active = -1; store->saved.nextRequest = 1; store->saved.serial = A2D_UNASSIGNED_SERIAL;
}
bool a2d_settings_load(A2D_SettingsStore *store, A2D_Calibration *calibration, uint8_t *address) {
    _Static_assert(sizeof(float) == 4 && sizeof(A2D_SettingsRecord) == 60, "Settings record layout changed");
    _Static_assert(offsetof(A2D_SettingsRecord, serial) == 48 && offsetof(A2D_SettingsRecord, crc) == 52, "Settings field offsets changed");
    A2D_SettingsRecord pages[2];
    a2d_flash_read_page(0, &pages[0], sizeof(pages[0]));
    a2d_flash_read_page(1, &pages[1], sizeof(pages[1]));
    bool legacy[2] = {false, false};
    for (uint8_t i = 0; i < 2; ++i) {
        /* V2.1.x CRC/commit occupied the next8 bytes after the common first48. */
        if (pages[i].magic == LegacyMagic && pages[i].crc == Commit && pages[i].serial == crc32(&pages[i], 48)) {
            pages[i].magic = Magic; pages[i].serial = A2D_UNASSIGNED_SERIAL;
            pages[i].commit = Commit; pages[i].crc = checksum(&pages[i]); legacy[i] = true;
        }
    }
    const bool a = valid(&pages[0]), b = valid(&pages[1]);
    store->active = a ? 0 : b ? 1 : -1;
    const uint32_t delta = pages[1].sequence - pages[0].sequence;
    if (a && b && delta && delta < 0x80000000UL) store->active = 1;
    if (store->active < 0) {
        /* Never restart request IDs from unknown or corrupt new-format records. */
        bool blank[2] = {true, true};
        for (uint8_t page = 0; page < 2; ++page) {
            const uint8_t *bytes = (const uint8_t *)&pages[page];
            for (size_t i = 0; i < sizeof(pages[page]); ++i) if (bytes[i] != 0xff) blank[page] = false;
        }
        const bool erased = blank[0] && blank[1];
        const bool old_format = blank[0] && (pages[1].magic == 0x00000055UL || pages[1].magic == 0xffffff55UL);
        store->saved.nextRequest = erased || old_format ? 1 : 0;
        return a2d_settings_initialize(store, calibration, address);
    }
    store->saved = pages[store->active];
    *address = (uint8_t)store->saved.address;
    bool repair = legacy[store->active];
    for (uint8_t i = 0; i < 4; ++i) if (!a2d_calibration_load(calibration, i, store->saved.calibration[i])) {
        a2d_calibration_reset(calibration, i); repair = true;
    }
    if (!repair) return true;
    /* Retain trusted serial/address/ID metadata when repairing calibration policy. */
    A2D_SettingsRecord record = store->saved;
    for (uint8_t i = 0; i < 4; ++i) record.calibration[i] = a2d_calibration_get(calibration, i);
    return save(store, record, true);
}
bool a2d_settings_initialize(A2D_SettingsStore *store, A2D_Calibration *calibration, uint8_t *address) {
    *address = 0; a2d_calibration_reset_all(calibration);
    A2D_SettingsRecord record = {0};
    record.nextRequest = store->saved.nextRequest; record.serial = store->saved.serial;
    for (uint8_t i = 0; i < 4; ++i) record.calibration[i] = a2d_calibration_get(calibration, i);
    return save(store, record, false);
}
bool a2d_settings_save_calibration(A2D_SettingsStore *store, const A2D_Calibration *calibration, uint8_t channel) {
    if (channel >= 4 || store->active < 0) return false;
    A2D_SettingsRecord record = store->saved; record.calibration[channel] = a2d_calibration_get(calibration, channel);
    return save(store, record, false);
}
bool a2d_settings_save_all_calibration(A2D_SettingsStore *store, const A2D_Calibration *calibration) {
    if (store->active < 0) return false;
    A2D_SettingsRecord record = store->saved;
    for (uint8_t i = 0; i < 4; ++i) record.calibration[i] = a2d_calibration_get(calibration, i);
    return save(store, record, false);
}
bool a2d_settings_save_address(A2D_SettingsStore *store, uint8_t address) {
    if (store->active < 0 || address > 31) return false;
    A2D_SettingsRecord record = store->saved; record.address = address;
    return save(store, record, false);
}
bool a2d_settings_reserve_request_ids(A2D_SettingsStore *store, uint32_t *first, uint32_t *last) {
    if (store->active < 0 || store->saved.nextRequest == 0) return false;
    A2D_SettingsRecord record = store->saved;
    const uint32_t begin = record.nextRequest;
    const uint32_t end = begin > 0xffff0000UL ? 0xffffffffUL : begin + 65535U;
    record.nextRequest = end == 0xffffffffUL ? 0 : end + 1;
    if (!save(store, record, false)) return false;
    /* Both fallback copies must advance before any ID is issued. */
    if (!save(store, store->saved, true)) return false;
    *first = begin; *last = end; return true;
}
uint32_t a2d_settings_serial(const A2D_SettingsStore *store) { return store->saved.serial; }
bool a2d_settings_provision_serial(A2D_SettingsStore *store, uint16_t serial) {
    if (store->active < 0 || serial > 9999 || (store->saved.serial != A2D_UNASSIGNED_SERIAL && store->saved.serial != serial)) return false;
    A2D_SettingsRecord other;
    a2d_flash_read_page((uint8_t)(1 - store->active), &other, sizeof(other));
    if (store->saved.serial == serial && valid(&other) && other.serial == serial) return true;
    A2D_SettingsRecord record = store->saved; record.serial = serial;
    return save(store, record, false) && save(store, store->saved, true);
}
