#ifndef A2D_SETTINGSSTORE_H
#define A2D_SETTINGSSTORE_H
#include "A2D_Calibration.h"
#ifdef __cplusplus
extern "C" {
#endif
#define A2D_UNASSIGNED_SERIAL UINT32_MAX
typedef struct {
    uint32_t magic, sequence;
    A2D_Coefficients calibration[4];
    uint32_t address, nextRequest, serial, crc, commit;
} A2D_SettingsRecord;
typedef struct { A2D_SettingsRecord saved; int8_t active; } A2D_SettingsStore;
void a2d_settings_init(A2D_SettingsStore *store);
bool a2d_settings_load(A2D_SettingsStore *store, A2D_Calibration *calibration, uint8_t *address);
bool a2d_settings_initialize(A2D_SettingsStore *store, A2D_Calibration *calibration, uint8_t *address);
bool a2d_settings_save_calibration(A2D_SettingsStore *store, const A2D_Calibration *calibration, uint8_t channel);
bool a2d_settings_save_all_calibration(A2D_SettingsStore *store, const A2D_Calibration *calibration);
bool a2d_settings_save_address(A2D_SettingsStore *store, uint8_t address);
bool a2d_settings_reserve_request_ids(A2D_SettingsStore *store, uint32_t *first, uint32_t *last);
uint32_t a2d_settings_serial(const A2D_SettingsStore *store);
bool a2d_settings_provision_serial(A2D_SettingsStore *store, uint16_t serial);

#ifdef __cplusplus
}
#endif
#endif
