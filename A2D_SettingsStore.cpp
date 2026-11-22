#include "A2D_SettingsStore.h"
#include "A2D_Flash.h"
#include <stddef.h>
#include <math.h>
#include <string.h>
namespace a2d {
namespace { const uint32_t Magic = 0x32444341UL, Commit = 0xc0dec0deUL; }
SettingsStore::SettingsStore() : saved_{}, active_(-1) { saved_.nextRequest = 1; }
uint32_t SettingsStore::checksum(const Record &record) {
    uint32_t crc = 0xffffffffUL;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
    for (size_t i = 0; i < offsetof(Record, crc); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1)));
    }
    return ~crc;
}
bool SettingsStore::valid(const Record &record) {
    // Recognize structurally valid older records before applying calibration
    // policy, so unusable coefficients do not discard trusted ID/address data.
    if (record.magic != Magic || record.commit != Commit || record.address > 31 || record.crc != checksum(record)) return false;
    for (const auto &cal : record.calibration)
        if (!isfinite(cal.gain) || cal.gain == 0 || !isfinite(cal.offset)) return false;
    return true;
}
bool SettingsStore::load(Calibration &calibration, uint8_t &address) {
    static_assert(sizeof(float) == 4 && sizeof(Record) == 56, "Settings record layout changed");
    Record pages[2];
    readSettingsPage(0, &pages[0], sizeof(Record));
    readSettingsPage(1, &pages[1], sizeof(Record));
    const bool a = valid(pages[0]), b = valid(pages[1]);
    active_ = a ? 0 : b ? 1 : -1;
    const uint32_t delta = pages[1].sequence - pages[0].sequence;
    if (a && b && delta && delta < 0x80000000UL) active_ = 1;
    if (active_ < 0) {
        // Only erased storage or the old non-ID format can start a new namespace.
        // Unknown/corrupt new records must fail closed for forwarding, not reuse IDs.
        bool blank[2] = {true, true};
        for (uint8_t page = 0; page < 2; ++page) {
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&pages[page]);
            for (size_t i = 0; i < sizeof(Record); ++i) if (bytes[i] != 0xff) blank[page] = false;
        }
        const bool erased = blank[0] && blank[1];
        const bool legacy = blank[0] && (pages[1].magic == 0x00000055UL || pages[1].magic == 0xffffff55UL);
        saved_.nextRequest = erased || legacy ? 1 : 0;
        return initialize(calibration, address);
    }
    saved_ = pages[active_];
    address = static_cast<uint8_t>(saved_.address);
    bool repair = false;
    for (uint8_t i = 0; i < 4; ++i) if (!calibration.load(i, saved_.calibration[i])) {
        calibration.reset(i);
        repair = true;
    }
    // Keep CRC-valid metadata and the ID high-water mark even when an older
    // image saved an implausible finite calibration. Repair only coefficients.
    return !repair || saveAllCalibration(calibration);
}
bool SettingsStore::save(Record record, bool force) {
    for (const auto &values : record.calibration) if (!Calibration::valid(values)) return false;
    if (!force && active_ >= 0 && record.address == saved_.address && record.nextRequest == saved_.nextRequest &&
        !memcmp(record.calibration, saved_.calibration, sizeof(record.calibration))) return true;
    record.magic = Magic;
    record.sequence = active_ < 0 ? 0 : saved_.sequence + 1;
    record.commit = Commit;
    record.crc = checksum(record);
    if (!valid(record)) return false;
    const uint8_t target = active_ == 0 ? 1 : 0;
    if (!writeSettingsPage(target, &record, sizeof(record))) return false;
    Record verify;
    readSettingsPage(target, &verify, sizeof(verify));
    if (!valid(verify) || memcmp(&verify, &record, sizeof(record))) return false;
    active_ = target; saved_ = record;
    return true;
}
bool SettingsStore::initialize(Calibration &calibration, uint8_t &address) {
    address = 0; calibration.resetAll();
    Record record = {};
    record.nextRequest = saved_.nextRequest;
    for (uint8_t i = 0; i < 4; ++i) record.calibration[i] = calibration.get(i);
    return save(record);
}
bool SettingsStore::saveCalibration(const Calibration &calibration, uint8_t channel) {
    if (channel >= 4 || active_ < 0) return false;
    Record record = saved_; record.calibration[channel] = calibration.get(channel);
    return save(record);
}
bool SettingsStore::saveAllCalibration(const Calibration &calibration) {
    if (active_ < 0) return false;
    Record record = saved_;
    for (uint8_t i = 0; i < 4; ++i) record.calibration[i] = calibration.get(i);
    return save(record);
}
bool SettingsStore::reserveRequestIds(uint32_t &first, uint32_t &last) {
    if (active_ < 0 || saved_.nextRequest == 0) return false;
    Record record = saved_;
    const uint32_t begin = record.nextRequest;
    const uint32_t end = begin > 0xffff0000UL ? 0xffffffffUL : begin + 65535U;
    record.nextRequest = end == 0xffffffffUL ? 0 : end + 1;
    if (!save(record)) return false;
    // Both recoverable copies must have the new high-water mark before any ID is issued.
    // Falling back after later CRC corruption can then never roll issued IDs backward.
    if (!save(saved_, true)) return false;
    first = begin; last = end; return true;
}
bool SettingsStore::saveAddress(uint8_t address) {
    if (active_ < 0 || address > 31) return false;
    Record record = saved_; record.address = address;
    return save(record);
}
}
