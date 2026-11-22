#ifndef A2D_SETTINGS_STORE_H
#define A2D_SETTINGS_STORE_H
#include "A2D_Calibration.h"
namespace a2d {
class SettingsStore {
public:
    SettingsStore();
    bool load(Calibration &calibration, uint8_t &address);
    bool initialize(Calibration &calibration, uint8_t &address);
    bool saveCalibration(const Calibration &calibration, uint8_t channel);
    bool saveAllCalibration(const Calibration &calibration);
    bool saveAddress(uint8_t address);
    bool reserveRequestIds(uint32_t &first, uint32_t &last);
private:
    struct Record {
        uint32_t magic, sequence;
        Coefficients calibration[4];
        uint32_t address, nextRequest, crc, commit;
    };
    static uint32_t checksum(const Record &record);
    static bool valid(const Record &record);
    bool save(Record record, bool force = false);
    Record saved_;
    int8_t active_;
};
}
#endif
