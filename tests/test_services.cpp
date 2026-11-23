#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <math.h>
#include <limits>
#include <stddef.h>
#include <string.h>
#include "A2D_4CH_Isolated_ADC.h"
#include "A2D_Flash.h"
#include "service_fakes/stm32f1xx_hal.h"
#include "service_fakes/Wire.h"
// Fault injection and legacy reconstruction below deliberately use deployed byte offsets.
static_assert(sizeof(float)==4 && sizeof(A2D_SettingsRecord)==60,"Settings fixture layout changed");
static_assert(offsetof(A2D_SettingsRecord,calibration)==8 && offsetof(A2D_SettingsRecord,nextRequest)==44 &&
              offsetof(A2D_SettingsRecord,serial)==48 && offsetof(A2D_SettingsRecord,crc)==52 &&
              offsetof(A2D_SettingsRecord,commit)==56,"Settings fixture offsets changed");
FakeI2c defaultBus;
static uint32_t test_clock=0;
static bool ledOn=true,rs485Receiving=false;
extern "C" A2D_I2c *a2d_default_i2c_bus(void){return &defaultBus.bus;}
extern "C" uint32_t A2D_millis(){return test_clock++;}
extern "C" void A2D_delay(uint32_t ms){test_clock+=ms;}
extern "C" void A2D_set_led(bool on){ledOn=on;}
extern "C" void A2D_set_rs485_receive(bool receive){rs485Receiving=receive;}
#define CHECK(c) do{if(!(c))throw std::runtime_error(#c);}while(0)
static unsigned erases=0,programs=0;
static int failAfter=-1;
static bool locked=true,failUnlock=false,corruptCommit=false;
bool operation(){if(failAfter==0)return false;if(failAfter>0)--failAfter;return true;}
extern "C" int HAL_FLASH_Unlock(void){if(failUnlock)return HAL_ERROR;locked=false;return HAL_OK;}
extern "C" int HAL_FLASH_Lock(void){locked=true;return HAL_OK;}
extern "C" int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *error){
    CHECK(!locked && e->TypeErase==FLASH_TYPEERASE_PAGES && e->NbPages==1);
    CHECK(e->PageAddress==A2D_SETTINGS_FLASH_BASE || e->PageAddress==A2D_SETTINGS_FLASH_BASE+1024);
    if(!operation())return HAL_ERROR;
    ++erases;*error=0;memset(reinterpret_cast<void *>(e->PageAddress),0xff,1024);return HAL_OK;
}
extern "C" int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t value){
    CHECK(!locked && type==FLASH_TYPEPROGRAM_HALFWORD && !(address&1));
    CHECK(address>=A2D_SETTINGS_FLASH_BASE && address<A2D_SETTINGS_FLASH_BASE+2048);
    if(!operation())return HAL_ERROR;
    ++programs;*reinterpret_cast<uint16_t *>(address)=static_cast<uint16_t>(value);
    if(corruptCommit && (address%1024)==58)*reinterpret_cast<uint8_t *>(address-50)^=1;
    return HAL_OK;
}
uint8_t *flash(){return reinterpret_cast<uint8_t *>(A2D_SETTINGS_FLASH_BASE);}
void cleanFlash(){memset(flash(),0xff,2048);erases=programs=0;failAfter=-1;locked=true;failUnlock=corruptCommit=false;}
bool approx(float a,float b){return fabs(a-b)<0.000001f;}
void calibration_math(){
    A2D_Calibration cal;a2d_calibration_init(&cal);CHECK(a2d_calibration_calibrate(&cal,2,0.25f,0.125f,4.375f,1.625f));
    CHECK(approx(a2d_calibration_apply(&cal,2,0.125f),0.25f)&&approx(a2d_calibration_apply(&cal,2,1.625f),4.375f));
    const auto old=a2d_calibration_get(&cal,2);
    CHECK(!a2d_calibration_calibrate(&cal,2,0,1,5,1));CHECK(!a2d_calibration_calibrate(&cal,2,0,0,0,1));
    CHECK(!a2d_calibration_calibrate(&cal,2,0,0,std::numeric_limits<float>::infinity(),1));
    CHECK(!a2d_calibration_calibrate(&cal,2,-3e38f,0,3e38f,1));CHECK(!a2d_calibration_calibrate(&cal,2,0,0,1e-30f,1e30f));
    CHECK(a2d_calibration_get(&cal,2).gain==old.gain && a2d_calibration_get(&cal,2).offset==old.offset);
    CHECK(isnan(a2d_calibration_apply(&cal,4,0.0f)));
    CHECK(isnan(a2d_calibration_apply(&cal,255,1.0f))); // Invalid channels must not resemble a valid 0 V reading.
}
void calibration_ranges(){
    A2D_Calibration cal;a2d_calibration_init(&cal);CHECK(a2d_calibration_calibrate(&cal,0,0,0,2.75f,1));const auto old=a2d_calibration_get(&cal,0);
    const A2D_Coefficients invalid[]={
        {nextafterf(A2D_CAL_MIN_GAIN,0),0},{nextafterf(A2D_CAL_MAX_GAIN,INFINITY),0},{-A2D_CAL_DEFAULT_GAIN,0},{0,0},
        {1e20f,0},{A2D_CAL_DEFAULT_GAIN,nextafterf(A2D_CAL_MAX_OFFSET,INFINITY)},
        {A2D_CAL_DEFAULT_GAIN,-nextafterf(A2D_CAL_MAX_OFFSET,INFINITY)},{NAN,0},{A2D_CAL_DEFAULT_GAIN,std::numeric_limits<float>::infinity()}};
    CHECK(a2d_calibration_valid({A2D_CAL_MIN_GAIN,-A2D_CAL_MAX_OFFSET}));CHECK(a2d_calibration_valid({A2D_CAL_MAX_GAIN,A2D_CAL_MAX_OFFSET}));
    for(const auto values:invalid){
        CHECK(!a2d_calibration_valid(values));CHECK(!a2d_calibration_load(&cal,0,values));
        CHECK(a2d_calibration_get(&cal,0).gain==old.gain&&a2d_calibration_get(&cal,0).offset==old.offset);
    }
    const float points[][4]={
        {-0.001f,0,5,2},{0,0,nextafterf(A2D_CAL_ACTUAL_MAX,INFINITY),2},
        {0,0.23f,5,nextafterf(A2D_CAL_MEASURED_MAX,INFINITY)},
        {0,nextafterf(-A2D_CAL_MEASURED_MAX,-INFINITY),5,2},
        {0,0,2,1},{0,0,4,1},{0,0,2.5f,1},{0,0,3.2f,1},
        {0,1,2.75f,0},{0,0.2501f,2.75f,1.2501f},
        {0,0,0.0014f,0.0005f},{0,0,1e20f,1}};
    for(const auto &p:points){
        A2D_Coefficients result={123,456};
        CHECK(!a2d_calibration_calculate(p[0],p[1],p[2],p[3],&result));
        CHECK(result.gain==123&&result.offset==456);
        CHECK(!a2d_calibration_calibrate(&cal,0,p[0],p[1],p[2],p[3]));
        CHECK(a2d_calibration_get(&cal,0).gain==old.gain&&a2d_calibration_get(&cal,0).offset==old.offset);
    }
    A2D_Coefficients result;
    CHECK(a2d_calibration_calculate(0,0,A2D_CAL_MIN_GAIN,1,&result));
    CHECK(a2d_calibration_calculate(0,0,A2D_CAL_MAX_GAIN,1,&result));
    CHECK(a2d_calibration_calculate(0,A2D_CAL_MAX_OFFSET,2.75f,1+A2D_CAL_MAX_OFFSET,&result));
    CHECK(a2d_calibration_calculate(0,-A2D_CAL_MAX_OFFSET,2.75f,1-A2D_CAL_MAX_OFFSET,&result));
    CHECK(a2d_calibration_calculate(0,0,A2D_CAL_MIN_SPAN*A2D_CAL_DEFAULT_GAIN,A2D_CAL_MIN_SPAN,&result));
    CHECK(!a2d_calibration_calculate(0,0,A2D_CAL_MIN_SPAN*A2D_CAL_DEFAULT_GAIN,nextafterf(A2D_CAL_MIN_SPAN,0),&result));
    CHECK(a2d_calibration_calculate(0,0.23f,5,A2D_CAL_MEASURED_MAX,&result));
    CHECK(a2d_calibration_calibrate(&cal,0,2.75f,1,0,0)); // Reversed endpoint order is valid; negative slope is not.
}
void adc_scaling_and_configuration(){
    FakeI2c bus;A2D_Measurement m={};CHECK(a2d_measurement_begin(&m,&bus.bus));CHECK(bus.resets==1 && defaultBus.transmissions==0);
    for(const auto c:bus.config)CHECK(c==0x18);
    for(uint8_t rate=0;rate<3;++rate)for(uint8_t gain=0;gain<4;++gain){
        CHECK(a2d_measurement_set_rate(&m,0,rate)&&a2d_measurement_set_gain(&m,0,gain));
        const int16_t code=static_cast<int16_t>(1024<<(rate*2));bus.codes[0]=code;
        CHECK(approx(a2d_measurement_read_raw(&m,0),1.024f/(1<<gain)));
        bus.codes[0]=-code;CHECK(approx(a2d_measurement_read_raw(&m,0),-1.024f/(1<<gain)));
    }
    CHECK(a2d_measurement_reset(&m));bus.codes[0]=-32768;CHECK(approx(a2d_measurement_read_raw(&m,0),-2.048f));
    CHECK(!a2d_measurement_set_gain(&m,0,4)&&!a2d_measurement_set_rate(&m,0,3)&&!a2d_measurement_set_mode(&m,0,2));
    CHECK(a2d_measurement_trigger(&m));for(const auto c:bus.config)CHECK(!(c&16));
    CHECK(a2d_measurement_set_gain(&m,0,1));CHECK(!(bus.config[0]&16));
    CHECK(a2d_measurement_set_rate(&m,0,1));CHECK(!(bus.config[0]&16));
}
void adc_failure_and_freshness(){
    FakeI2c bus;A2D_Measurement m={};CHECK(a2d_measurement_begin(&m,&bus.bus));bus.codes[0]=1600;
    const float good=a2d_measurement_read_raw(&m,0);CHECK(good>0.099f);
    for(uint8_t count=0;count<3;++count){bus.responseCount=count;CHECK(isnan(a2d_measurement_read_raw(&m,0)));}
    bus.responseCount=3;bus.wrongConfig=true;CHECK(isnan(a2d_measurement_read_raw(&m,0)));bus.wrongConfig=false;
    CHECK(a2d_measurement_set_gain(&m,0,3));bus.holdNotReady=true;test_clock=0;
    CHECK(isnan(a2d_measurement_read_raw(&m,0)) && test_clock>=120 && test_clock<130);
    bus.holdNotReady=false;bus.codes[0]=12800;CHECK(approx(a2d_measurement_read_raw(&m,0),good));
    CHECK(a2d_measurement_reset(&m));bus.holdNotReady=true;CHECK(isnan(a2d_measurement_read_raw(&m,0)));bus.holdNotReady=false;
    bus.failWrites=true;CHECK(!a2d_measurement_set_gain(&m,0,2));CHECK(isnan(a2d_measurement_read_raw(&m,0)));
    CHECK(!a2d_measurement_trigger(&m));for(uint8_t ch=0;ch<4;++ch)CHECK(isnan(a2d_measurement_read_raw(&m,ch)));
    bus.failWrites=false;CHECK(a2d_measurement_reset(&m));CHECK(isfinite(a2d_measurement_read_raw(&m,0)));
    CHECK(isnan(a2d_measurement_read_raw(&m,255)));
}
void storage_save_boundaries(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address=7;
    CHECK(a2d_settings_load(&store,&cal,&address)&&address==0&&erases==1);
    const unsigned oldErases=erases;CHECK(a2d_settings_save_all_calibration(&store,&cal)&&erases==oldErases);
    for(uint8_t ch=0;ch<4;++ch)CHECK(a2d_calibration_calibrate(&cal,ch,0.25f,0.125f,4.375f,1.625f));
    CHECK(a2d_settings_save_all_calibration(&store,&cal)&&erases==oldErases+1); // One page erase for all32 coefficient bytes.
    const float savedGain=a2d_calibration_get(&cal,2).gain;
    CHECK(a2d_calibration_calibrate(&cal,2,0,0,3.0f,1));CHECK(a2d_calibration_calibrate(&cal,1,0,0,3,1));
    CHECK(a2d_settings_save_calibration(&store,&cal,1));CHECK(a2d_settings_save_address(&store,17));
    A2D_SettingsStore reload;a2d_settings_init(&reload);A2D_Calibration loaded;a2d_calibration_init(&loaded);CHECK(a2d_settings_load(&reload,&loaded,&address)&&address==17);
    CHECK(a2d_calibration_get(&loaded,1).gain==3 && a2d_calibration_get(&loaded,2).gain==savedGain);
    CHECK(!a2d_settings_save_address(&reload,255)&&!a2d_settings_save_calibration(&reload,&cal,4));
    CHECK(locked);
}
void storage_power_loss_every_operation(){
    cleanFlash();A2D_Calibration initial;a2d_calibration_init(&initial);A2D_SettingsStore first;a2d_settings_init(&first);uint8_t address;
    CHECK(a2d_settings_load(&first,&initial,&address));uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    for(int cut=0;cut<=31;++cut){
        memcpy(flash(),baseline,2048);failAfter=-1;
        A2D_SettingsStore store;a2d_settings_init(&store);A2D_Calibration cal;a2d_calibration_init(&cal);CHECK(a2d_settings_load(&store,&cal,&address));
        a2d_calibration_calibrate(&cal,0,0,0,2.75f,1);failAfter=cut;const bool saved=a2d_settings_save_all_calibration(&store,&cal);failAfter=-1;
        CHECK(!memcmp(flash(),baseline,1024)); // Previously committed page is never erased/programmed.
        A2D_SettingsStore reboot;a2d_settings_init(&reboot);A2D_Calibration restored;a2d_calibration_init(&restored);CHECK(a2d_settings_load(&reboot,&restored,&address));
        if(cut<31)CHECK(!saved && a2d_calibration_get(&restored,0).gain==a2d_calibration_get(&initial,0).gain);
        else CHECK(saved && a2d_calibration_get(&restored,0).gain==2.75f);
    }
    memcpy(flash(),baseline,2048);A2D_SettingsStore store;a2d_settings_init(&store);A2D_Calibration cal;a2d_calibration_init(&cal);CHECK(a2d_settings_load(&store,&cal,&address));
    a2d_calibration_calibrate(&cal,0,0,0,2.75f,1);corruptCommit=true;CHECK(!a2d_settings_save_all_calibration(&store,&cal));corruptCommit=false;
    A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));CHECK(a2d_calibration_get(&cal,0).gain==a2d_calibration_get(&initial,0).gain);
    failUnlock=true;CHECK(!a2d_settings_save_address(&store,5));failUnlock=false;CHECK(locked);
}
void bootstrap_interruption_fails_closed(){
    // One erase plus 30 halfword writes. A torn first record is an unknown
    // namespace: preserve fail-closed behavior until manufacturing/service recovery.
    for(int cut=0;cut<=31;++cut){
        cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);
        A2D_SettingsStore initial;a2d_settings_init(&initial);uint8_t address=7;
        failAfter=cut;const bool initialized=a2d_settings_load(&initial,&cal,&address);failAfter=-1;
        CHECK(initialized==(cut==31));CHECK(locked);
        A2D_SettingsStore reboot;a2d_settings_init(&reboot);
        CHECK(a2d_settings_load(&reboot,&cal,&address)&&address==0);
        for(uint8_t channel=0;channel<4;++channel){
            CHECK(a2d_calibration_get(&cal,channel).gain==A2D_CAL_DEFAULT_GAIN);
            CHECK(a2d_calibration_get(&cal,channel).offset==0);
        }
        uint32_t first=0xaaaaaaaaUL,last=0xbbbbbbbbUL;
        if(cut>=2 && cut<31){
            // At least one halfword changed, but no valid commit exists.
            CHECK(!a2d_settings_reserve_request_ids(&reboot,&first,&last));
            CHECK(first==0xaaaaaaaaUL&&last==0xbbbbbbbbUL);
            CHECK(a2d_settings_initialize(&reboot,&cal,&address));
            CHECK(!a2d_settings_reserve_request_ids(&reboot,&first,&last));
            CHECK(a2d_settings_provision_serial(&reboot,3));
            CHECK(!a2d_settings_reserve_request_ids(&reboot,&first,&last));
            // Neither a normal defaults reset nor successful serial provisioning
            // authorizes starting a new request-ID namespace.
            A2D_SettingsStore again;a2d_settings_init(&again);
            CHECK(a2d_settings_load(&again,&cal,&address)&&a2d_settings_serial(&again)==3);
            CHECK(!a2d_settings_reserve_request_ids(&again,&first,&last));
            CHECK(first==0xaaaaaaaaUL&&last==0xbbbbbbbbUL);
        }else{
            // Cuts 0/1 leave storage erased; cut 31 commits the initial record.
            CHECK(a2d_settings_reserve_request_ids(&reboot,&first,&last)&&first==1&&last==65536);
        }
    }
}
void request_ids_survive_reboot(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;uint32_t first,last;
    CHECK(a2d_settings_load(&store,&cal,&address));CHECK(a2d_settings_reserve_request_ids(&store,&first,&last)&&first==1&&last==65536);
    A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));
    CHECK(a2d_settings_reserve_request_ids(&reboot,&first,&last)&&first==65537&&last==131072);
    CHECK(a2d_settings_initialize(&reboot,&cal,&address)); // Factory calibration reset must not reset IDs.
    A2D_SettingsStore again;a2d_settings_init(&again);CHECK(a2d_settings_load(&again,&cal,&address));
    CHECK(a2d_settings_reserve_request_ids(&again,&first,&last)&&first==131073);
}
void request_reservations_survive_corruption_and_torn_mirror(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;uint32_t first,last;
    CHECK(a2d_settings_load(&store,&cal,&address));CHECK(a2d_settings_reserve_request_ids(&store,&first,&last)&&first==1);
    uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    for(unsigned page=0;page<2;++page){
        memcpy(flash(),baseline,2048);flash()[page*1024+8]^=1;
        A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));
        CHECK(a2d_settings_reserve_request_ids(&reboot,&first,&last)&&first>=65537); // ID1 cannot be reused through either fallback.
    }
    for(int cut=0;cut<=62;++cut){
        memcpy(flash(),baseline,2048);failAfter=-1;
        A2D_SettingsStore before;a2d_settings_init(&before);CHECK(a2d_settings_load(&before,&cal,&address));failAfter=cut;
        const bool reserved=a2d_settings_reserve_request_ids(&before,&first,&last);failAfter=-1;
        const uint32_t highestIssued=reserved?first:1;
        A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));
        CHECK(a2d_settings_reserve_request_ids(&reboot,&first,&last)&&first>highestIssued);
    }
}
void repairTestRecordCrc(uint8_t *record){
    uint32_t crc=0xffffffffUL;
    for(unsigned i=0;i<52;++i){crc^=record[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320UL:0);}
    crc=~crc;memcpy(record+52,&crc,4);
}
void stored_calibration_range_repair_preserves_ids(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;uint32_t first,last;
    CHECK(a2d_settings_load(&store,&cal,&address));CHECK(a2d_calibration_calibrate(&cal,1,0,0,2.75f,1));
    CHECK(a2d_settings_save_all_calibration(&store,&cal)&&a2d_settings_save_address(&store,7));
    CHECK(a2d_settings_reserve_request_ids(&store,&first,&last)&&first==1&&last==65536);
    uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    // Simulate CRC-valid records saved by an older image, not arbitrary corrupt metadata.
    for(int cut=0;cut<=31;++cut){
        memcpy(flash(),baseline,2048);
        for(unsigned page=0;page<2;++page){float huge=1e20f;memcpy(flash()+1024*page+8,&huge,4);repairTestRecordCrc(flash()+1024*page);}
        failAfter=cut;A2D_SettingsStore reboot;a2d_settings_init(&reboot);A2D_Calibration loaded;a2d_calibration_init(&loaded);
        const bool ok=a2d_settings_load(&reboot,&loaded,&address);failAfter=-1;
        CHECK(ok==(cut>=31));CHECK(address==7);
        CHECK(a2d_calibration_get(&loaded,0).gain==A2D_CAL_DEFAULT_GAIN&&a2d_calibration_get(&loaded,1).gain==2.75f);
        A2D_SettingsStore retry;a2d_settings_init(&retry);A2D_Calibration retried;a2d_calibration_init(&retried);CHECK(a2d_settings_load(&retry,&retried,&address));
        CHECK(address==7&&a2d_calibration_get(&retried,0).gain==A2D_CAL_DEFAULT_GAIN&&a2d_calibration_get(&retried,1).gain==2.75f);
        CHECK(a2d_settings_reserve_request_ids(&retry,&first,&last)&&first>=65537);
    }
}
void request_id_exhaustion_and_sequence_wrap(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;uint32_t first,last;
    CHECK(a2d_settings_load(&store,&cal,&address));
    uint32_t next=0xfffffff0UL,sequence=0xffffffffUL;memcpy(flash()+44,&next,4);memcpy(flash()+4,&sequence,4);repairTestRecordCrc(flash());
    A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));
    CHECK(a2d_settings_reserve_request_ids(&reboot,&first,&last)&&first==0xfffffff0UL&&last==0xffffffffUL);
    A2D_SettingsStore wrapped;a2d_settings_init(&wrapped);CHECK(a2d_settings_load(&wrapped,&cal,&address));CHECK(!a2d_settings_reserve_request_ids(&wrapped,&first,&last));
    CHECK(a2d_settings_initialize(&wrapped,&cal,&address));CHECK(!a2d_settings_reserve_request_ids(&wrapped,&first,&last));
}
void storage_corrupt_record_recovery(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;
    CHECK(a2d_settings_load(&store,&cal,&address));a2d_calibration_calibrate(&cal,0,0,0,2.75f,1);CHECK(a2d_settings_save_all_calibration(&store,&cal));
    flash()[1024+8]^=1; // Corrupt newest copy; CRC rejects it and prior copy remains usable.
    A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));CHECK(approx(a2d_calibration_get(&cal,0).gain,2.818181818f));
    flash()[8]^=1; // Both invalid: initialize finite nominal defaults, never load NaN/address255.
    A2D_SettingsStore empty;a2d_settings_init(&empty);CHECK(a2d_settings_load(&empty,&cal,&address)&&address==0);
    for(uint8_t ch=0;ch<4;++ch)CHECK(isfinite(a2d_calibration_get(&cal,ch).gain)&&a2d_calibration_get(&cal,ch).gain!=0);
    uint32_t first,last;CHECK(!a2d_settings_reserve_request_ids(&empty,&first,&last));
}
void board_reset_and_storage_errors(){
    cleanFlash();FakeI2c bus;A2D_Board board={};CHECK(a2d_board_init(&board,&bus.bus));
    CHECK(!ledOn && rs485Receiving);
    CHECK(a2d_board_calibrate(&board,0,0.25f,0.125f,4.375f,1.625f));const float gain=a2d_board_cal_gain(&board,0),offset=a2d_board_cal_offset(&board,0);
    CHECK(a2d_board_save_all_calibration(&board));a2d_board_set_address(&board,7);CHECK(a2d_board_save_address(&board));
    a2d_board_reset_calibration(&board,0);a2d_board_set_address(&board,1);CHECK(a2d_board_reset(&board));
    CHECK(a2d_board_address(&board)==7 && a2d_board_cal_gain(&board,0)==gain && a2d_board_cal_offset(&board,0)==offset);
    bus.codes[0]=16384;CHECK(approx(a2d_board_measure_voltage(&board,0),(1.024f-offset)*gain));
    CHECK(isnan(a2d_board_measure_voltage(&board,4)));
    CHECK(isnan(a2d_board_measure_voltage(&board,255)));
    bus.responseCount=0;CHECK(isnan(a2d_board_measure_voltage(&board,0)));bus.responseCount=3;
    CHECK(a2d_board_calibrate(&board,0,0,0,3,1));failAfter=0;CHECK(!a2d_board_save_all_calibration(&board));failAfter=-1;
    CHECK(a2d_board_reset(&board) && a2d_board_cal_gain(&board,0)==gain);
    a2d_board_set_rs485_receive(&board,false);CHECK(!rs485Receiving);a2d_board_set_rs485_receive(&board,true);CHECK(rs485Receiving);
}
void persistent_serial_identity(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore store;a2d_settings_init(&store);uint8_t address;uint32_t first,last;
    CHECK(a2d_settings_load(&store,&cal,&address)&&a2d_settings_serial(&store)==A2D_UNASSIGNED_SERIAL);
    CHECK(a2d_settings_provision_serial(&store,3));const unsigned savedErases=erases;
    CHECK(a2d_settings_provision_serial(&store,3)&&erases==savedErases);CHECK(!a2d_settings_provision_serial(&store,4));
    CHECK(a2d_settings_save_address(&store,7)&&a2d_settings_reserve_request_ids(&store,&first,&last));
    CHECK(a2d_settings_initialize(&store,&cal,&address)&&a2d_settings_serial(&store)==3);
    A2D_SettingsStore again;a2d_settings_init(&again);CHECK(a2d_settings_load(&again,&cal,&address)&&a2d_settings_serial(&again)==3);
    CHECK(a2d_settings_reserve_request_ids(&again,&first,&last)&&first>=65537);
    FakeI2c bus;A2D_Board board={};CHECK(a2d_board_init(&board,&bus.bus));
    CHECK(!strcmp(a2d_board_serial(&board),"0003"));CHECK(a2d_board_initialize_settings(&board));
    CHECK(!strcmp(a2d_board_serial(&board),"0003")&&!a2d_board_provision_serial(&board,4));
}
void serial_power_loss_and_legacy_migration(){
    cleanFlash();A2D_Calibration cal;a2d_calibration_init(&cal);A2D_SettingsStore initial;a2d_settings_init(&initial);uint8_t address;
    CHECK(a2d_settings_load(&initial,&cal,&address));uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    for(int cut=0;cut<=62;++cut){
        memcpy(flash(),baseline,2048);failAfter=-1;
        A2D_SettingsStore store;a2d_settings_init(&store);CHECK(a2d_settings_load(&store,&cal,&address));failAfter=cut;
        const bool committed=a2d_settings_provision_serial(&store,3);failAfter=-1;
        A2D_SettingsStore reboot;a2d_settings_init(&reboot);CHECK(a2d_settings_load(&reboot,&cal,&address));
        CHECK(a2d_settings_serial(&reboot)==3||a2d_settings_serial(&reboot)==A2D_UNASSIGNED_SERIAL);
        if(committed)CHECK(a2d_settings_serial(&reboot)==3);
        CHECK(a2d_settings_provision_serial(&reboot,3));
        for(unsigned page=0;page<2;++page){
            uint8_t mirrored[2048];memcpy(mirrored,flash(),2048);flash()[page*1024+8]^=1;
            A2D_SettingsStore fallback;a2d_settings_init(&fallback);CHECK(a2d_settings_load(&fallback,&cal,&address)&&a2d_settings_serial(&fallback)==3);
            memcpy(flash(),mirrored,2048);
        }
    }
    // Reconstruct a deployed V2.1.x56-byte record with retained coefficients,
    // address and ID high-water mark; the old CRC/commit are at48/52.
    cleanFlash();A2D_SettingsStore old;a2d_settings_init(&old);CHECK(a2d_settings_load(&old,&cal,&address));
    CHECK(a2d_calibration_calibrate(&cal,1,0,0,2.75f,1)&&a2d_settings_save_all_calibration(&old,&cal)&&a2d_settings_save_address(&old,17));
    uint32_t first,last;CHECK(a2d_settings_reserve_request_ids(&old,&first,&last));
    for(unsigned page=0;page<2;++page){
        uint8_t *record=flash()+page*1024;uint32_t magic=0x32444341UL,crc=0xffffffffUL,commit=0xc0dec0deUL;
        memcpy(record,&magic,4);
        for(unsigned i=0;i<48;++i){crc^=record[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320UL:0);}
        crc=~crc;memcpy(record+48,&crc,4);memcpy(record+52,&commit,4);memset(record+56,0xff,4);
    }
    A2D_SettingsStore migrated;a2d_settings_init(&migrated);CHECK(a2d_settings_load(&migrated,&cal,&address));
    CHECK(address==17&&a2d_calibration_get(&cal,1).gain==2.75f&&a2d_settings_serial(&migrated)==A2D_UNASSIGNED_SERIAL);
    CHECK(a2d_settings_provision_serial(&migrated,3)&&a2d_settings_reserve_request_ids(&migrated,&first,&last)&&first>=65537);
}
void configuration_readback_all_settings(){
    cleanFlash();FakeI2c bus;A2D_Board board={};CHECK(a2d_board_init(&board,&bus.bus));
    const unsigned initial_erases=erases,initial_programs=programs;
    uint8_t gain=99,rate=99,mode=99;
    CHECK(a2d_board_get_configuration(&board,0,&gain,&rate,&mode)&&gain==1&&rate==15&&mode==1);
    const uint8_t rates[]={240,60,15};
    bus.holdNotReady=true; // A configuration query must not wait for ADC conversion readiness.
    for(uint8_t channel=0;channel<4;++channel)for(uint8_t g=0;g<4;++g)for(uint8_t d=0;d<3;++d)for(uint8_t m=0;m<2;++m){
        CHECK(a2d_board_set_gain(&board,channel,g)&&a2d_board_set_data_rate(&board,channel,d)&&a2d_board_set_mode(&board,channel,m));
        const unsigned writes=bus.transmissions,reads=bus.requests;const auto clock=test_clock;
        CHECK(a2d_board_get_configuration(&board,channel,&gain,&rate,&mode));
        CHECK(gain==(1U<<g)&&rate==rates[d]&&mode==m);
        CHECK(bus.transmissions==writes&&bus.requests==reads+1&&test_clock==clock);
    }
    CHECK(a2d_board_trigger(&board));
    for(uint8_t channel=0;channel<4;++channel)CHECK(a2d_board_get_configuration(&board,channel,&gain,&rate,&mode)&&mode==0);
    CHECK(a2d_board_reset(&board));
    for(uint8_t channel=0;channel<4;++channel)CHECK(a2d_board_get_configuration(&board,channel,&gain,&rate,&mode)&&gain==1&&rate==15&&mode==1);
    CHECK(erases==initial_erases&&programs==initial_programs);
}
void configuration_readback_errors_do_not_publish(){
    FakeI2c bus;A2D_Measurement measurement={};CHECK(a2d_measurement_begin(&measurement,&bus.bus));
    uint8_t value=123;
    CHECK(!a2d_measurement_get_configuration(&measurement,4,&value)&&value==123);
    CHECK(!a2d_measurement_get_configuration(&measurement,255,&value)&&value==123);
    for(uint8_t count=0;count<3;++count){bus.responseCount=count;CHECK(!a2d_measurement_get_configuration(&measurement,0,&value)&&value==123);}
    bus.responseCount=3;bus.wrongConfig=true;CHECK(!a2d_measurement_get_configuration(&measurement,0,&value)&&value==123);bus.wrongConfig=false;
    bus.failWrites=true;CHECK(!a2d_measurement_set_gain(&measurement,0,1));const unsigned reads=bus.requests;
    CHECK(!a2d_measurement_get_configuration(&measurement,0,&value)&&value==123&&bus.requests==reads);
    bus.failWrites=false;CHECK(a2d_measurement_set_gain(&measurement,0,1));
    CHECK(a2d_measurement_get_configuration(&measurement,0,&value)&&value==0x19);
    value=123;measurement.config[0]=bus.config[0]=0x1c;
    CHECK(!a2d_measurement_get_configuration(&measurement,0,&value)&&value==123); // Reserved resolution must not index the public rate table.
}
int main(){
    CHECK(VirtualAlloc(reinterpret_cast<void *>(0x08000000),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reinterpret_cast<void *>(0x08000000));
    const struct{const char *name;void(*run)();} tests[]={
        {"configuration readback all settings",configuration_readback_all_settings},
        {"configuration readback failure publication",configuration_readback_errors_do_not_publish},
        {"calibration math",calibration_math},{"calibration input and coefficient ranges",calibration_ranges},
        {"stored calibration range repair preserves IDs",stored_calibration_range_repair_preserves_ids},
        {"ADC scaling and trigger shadows",adc_scaling_and_configuration},
        {"ADC failure and freshness",adc_failure_and_freshness},{"settings batching and save boundaries",storage_save_boundaries},
        {"interrupted first initialization fails closed",bootstrap_interruption_fails_closed},
        {"power loss at every flash operation",storage_power_loss_every_operation},{"corrupt-record recovery",storage_corrupt_record_recovery},{"request IDs survive reboot",request_ids_survive_reboot},{"ID exhaustion and generation wrap",request_id_exhaustion_and_sequence_wrap},{"ID fallback and interrupted mirror",request_reservations_survive_corruption_and_torn_mirror},
        {"board persistence and error propagation",board_reset_and_storage_errors},
        {"persistent serial identity",persistent_serial_identity},{"serial power loss and legacy migration",serial_power_loss_and_legacy_migration}};
    for(const auto &t:tests){try{t.run();}catch(const std::exception &e){std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<'\n';return 1;}std::cout<<"PASS "<<t.name<<'\n';}
}
