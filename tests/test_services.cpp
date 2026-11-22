#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <math.h>
#include <limits>
#include "A2D_4CH_Isolated_ADC.h"
#include "A2D_Flash.h"
TwoWire Wire; unsigned long test_clock=0; int pin_values[32]={};
#define CHECK(c) do{if(!(c))throw std::runtime_error(#c);}while(0)
static unsigned erases=0,programs=0;
static int failAfter=-1;
static bool locked=true,failUnlock=false,corruptCommit=false;
bool operation(){if(failAfter==0)return false;if(failAfter>0)--failAfter;return true;}
int HAL_FLASH_Unlock(){if(failUnlock)return HAL_ERROR;locked=false;return HAL_OK;}
int HAL_FLASH_Lock(){locked=true;return HAL_OK;}
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *error){
    CHECK(!locked && e->TypeErase==FLASH_TYPEERASE_PAGES && e->NbPages==1);
    CHECK(e->PageAddress==a2d::SettingsFlashBase || e->PageAddress==a2d::SettingsFlashBase+1024);
    if(!operation())return HAL_ERROR;
    ++erases;*error=0;memset(reinterpret_cast<void *>(e->PageAddress),0xff,1024);return HAL_OK;
}
int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t value){
    CHECK(!locked && type==FLASH_TYPEPROGRAM_HALFWORD && !(address&1));
    CHECK(address>=a2d::SettingsFlashBase && address<a2d::SettingsFlashBase+2048);
    if(!operation())return HAL_ERROR;
    ++programs;*reinterpret_cast<uint16_t *>(address)=static_cast<uint16_t>(value);
    if(corruptCommit && (address%1024)==54)*reinterpret_cast<uint8_t *>(address-46)^=1;
    return HAL_OK;
}
uint8_t *flash(){return reinterpret_cast<uint8_t *>(a2d::SettingsFlashBase);}
void cleanFlash(){memset(flash(),0xff,2048);erases=programs=0;failAfter=-1;locked=true;failUnlock=corruptCommit=false;}
bool approx(float a,float b){return fabs(a-b)<0.000001f;}
void calibration_math(){
    a2d::Calibration cal;CHECK(cal.calibrate(2,0.25f,0.125f,4.375f,1.625f));
    CHECK(approx(cal.apply(2,0.125f),0.25f)&&approx(cal.apply(2,1.625f),4.375f));
    const auto old=cal.get(2);
    CHECK(!cal.calibrate(2,0,1,5,1));CHECK(!cal.calibrate(2,0,0,0,1));
    CHECK(!cal.calibrate(2,0,0,std::numeric_limits<float>::infinity(),1));
    CHECK(!cal.calibrate(2,-3e38f,0,3e38f,1));CHECK(!cal.calibrate(2,0,0,1e-30f,1e30f));
    CHECK(cal.get(2).gain==old.gain && cal.get(2).offset==old.offset);
}
void calibration_ranges(){
    using namespace a2d::CalibrationLimits;
    a2d::Calibration cal;CHECK(cal.calibrate(0,0,0,2.75f,1));const auto old=cal.get(0);
    const a2d::Coefficients invalid[]={
        {nextafterf(MinGain,0),0},{nextafterf(MaxGain,INFINITY),0},{-DefaultGain,0},{0,0},
        {1e20f,0},{DefaultGain,nextafterf(MaxOffset,INFINITY)},
        {DefaultGain,-nextafterf(MaxOffset,INFINITY)},{NAN,0},{DefaultGain,std::numeric_limits<float>::infinity()}};
    CHECK(a2d::Calibration::valid({MinGain,-MaxOffset}));CHECK(a2d::Calibration::valid({MaxGain,MaxOffset}));
    for(const auto values:invalid){
        CHECK(!a2d::Calibration::valid(values));CHECK(!cal.load(0,values));
        CHECK(cal.get(0).gain==old.gain&&cal.get(0).offset==old.offset);
    }
    const float points[][4]={
        {-0.001f,0,5,2},{0,0,nextafterf(ActualMax,INFINITY),2},
        {0,0.23f,5,nextafterf(MeasuredMax,INFINITY)},
        {0,nextafterf(-MeasuredMax,-INFINITY),5,2},
        {0,0,2,1},{0,0,4,1},{0,0,2.5f,1},{0,0,3.2f,1},
        {0,1,2.75f,0},{0,0.2501f,2.75f,1.2501f},
        {0,0,0.0014f,0.0005f},{0,0,1e20f,1}};
    for(const auto &p:points){
        a2d::Coefficients result={123,456};
        CHECK(!a2d::Calibration::calculate(p[0],p[1],p[2],p[3],result));
        CHECK(result.gain==123&&result.offset==456);
        CHECK(!cal.calibrate(0,p[0],p[1],p[2],p[3]));
        CHECK(cal.get(0).gain==old.gain&&cal.get(0).offset==old.offset);
    }
    a2d::Coefficients result;
    CHECK(a2d::Calibration::calculate(0,0,MinGain,1,result));
    CHECK(a2d::Calibration::calculate(0,0,MaxGain,1,result));
    CHECK(a2d::Calibration::calculate(0,MaxOffset,2.75f,1+MaxOffset,result));
    CHECK(a2d::Calibration::calculate(0,-MaxOffset,2.75f,1-MaxOffset,result));
    CHECK(a2d::Calibration::calculate(0,0,MinMeasuredSpan*DefaultGain,MinMeasuredSpan,result));
    CHECK(!a2d::Calibration::calculate(0,0,MinMeasuredSpan*DefaultGain,nextafterf(MinMeasuredSpan,0),result));
    CHECK(a2d::Calibration::calculate(0,0.23f,5,MeasuredMax,result));
    CHECK(cal.calibrate(0,2.75f,1,0,0)); // Reversed endpoint order is valid; negative slope is not.
}
void adc_scaling_and_configuration(){
    TwoWire bus;a2d::Measurement m;CHECK(m.begin(&bus));CHECK(bus.resets==1 && Wire.transmissions==0);
    for(const auto c:bus.config)CHECK(c==0x18);
    for(uint8_t rate=0;rate<3;++rate)for(uint8_t gain=0;gain<4;++gain){
        CHECK(m.setRate(0,rate)&&m.setGain(0,gain));
        const int16_t code=static_cast<int16_t>(1024<<(rate*2));bus.codes[0]=code;
        CHECK(approx(m.readRaw(0),1.024f/(1<<gain)));
        bus.codes[0]=-code;CHECK(approx(m.readRaw(0),-1.024f/(1<<gain)));
    }
    CHECK(m.reset());bus.codes[0]=-32768;CHECK(approx(m.readRaw(0),-2.048f));
    CHECK(!m.setGain(0,4)&&!m.setRate(0,3)&&!m.setMode(0,2));
    CHECK(m.trigger());for(const auto c:bus.config)CHECK(!(c&16));
    CHECK(m.setGain(0,1));CHECK(!(bus.config[0]&16));
    CHECK(m.setRate(0,1));CHECK(!(bus.config[0]&16));
}
void adc_failure_and_freshness(){
    TwoWire bus;a2d::Measurement m;CHECK(m.begin(&bus));bus.codes[0]=1600;
    const float good=m.readRaw(0);CHECK(good>0.099f);
    for(uint8_t count=0;count<3;++count){bus.responseCount=count;CHECK(isnan(m.readRaw(0)));}
    bus.responseCount=3;bus.wrongConfig=true;CHECK(isnan(m.readRaw(0)));bus.wrongConfig=false;
    CHECK(m.setGain(0,3));bus.holdNotReady=true;test_clock=0;
    CHECK(isnan(m.readRaw(0)) && test_clock>=120 && test_clock<130);
    bus.holdNotReady=false;bus.codes[0]=12800;CHECK(approx(m.readRaw(0),good));
    CHECK(m.reset());bus.holdNotReady=true;CHECK(isnan(m.readRaw(0)));bus.holdNotReady=false;
    bus.failWrites=true;CHECK(!m.setGain(0,2));CHECK(isnan(m.readRaw(0)));
    CHECK(!m.trigger());for(uint8_t ch=0;ch<4;++ch)CHECK(isnan(m.readRaw(ch)));
    bus.failWrites=false;CHECK(m.reset());CHECK(isfinite(m.readRaw(0)));
    CHECK(isnan(m.readRaw(255)));
}
void storage_save_boundaries(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address=7;
    CHECK(store.load(cal,address)&&address==0&&erases==1);
    const unsigned oldErases=erases;CHECK(store.saveAllCalibration(cal)&&erases==oldErases);
    for(uint8_t ch=0;ch<4;++ch)CHECK(cal.calibrate(ch,0.25f,0.125f,4.375f,1.625f));
    CHECK(store.saveAllCalibration(cal)&&erases==oldErases+1); // One page erase for all32 coefficient bytes.
    const float savedGain=cal.get(2).gain;
    CHECK(cal.calibrate(2,0,0,3.0f,1));CHECK(cal.calibrate(1,0,0,3,1));
    CHECK(store.saveCalibration(cal,1));CHECK(store.saveAddress(17));
    a2d::SettingsStore reload;a2d::Calibration loaded;CHECK(reload.load(loaded,address)&&address==17);
    CHECK(loaded.get(1).gain==3 && loaded.get(2).gain==savedGain);
    CHECK(!reload.saveAddress(255)&&!reload.saveCalibration(cal,4));
    CHECK(locked);
}
void storage_power_loss_every_operation(){
    cleanFlash();a2d::Calibration initial;a2d::SettingsStore first;uint8_t address;
    CHECK(first.load(initial,address));uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    for(int cut=0;cut<=29;++cut){
        memcpy(flash(),baseline,2048);failAfter=-1;
        a2d::SettingsStore store;a2d::Calibration cal;CHECK(store.load(cal,address));
        cal.calibrate(0,0,0,2.75f,1);failAfter=cut;const bool saved=store.saveAllCalibration(cal);failAfter=-1;
        CHECK(!memcmp(flash(),baseline,1024)); // Previously committed page is never erased/programmed.
        a2d::SettingsStore reboot;a2d::Calibration restored;CHECK(reboot.load(restored,address));
        if(cut<29)CHECK(!saved && restored.get(0).gain==initial.get(0).gain);
        else CHECK(saved && restored.get(0).gain==2.75f);
    }
    memcpy(flash(),baseline,2048);a2d::SettingsStore store;a2d::Calibration cal;CHECK(store.load(cal,address));
    cal.calibrate(0,0,0,2.75f,1);corruptCommit=true;CHECK(!store.saveAllCalibration(cal));corruptCommit=false;
    a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));CHECK(cal.get(0).gain==initial.get(0).gain);
    failUnlock=true;CHECK(!store.saveAddress(5));failUnlock=false;CHECK(locked);
}
void request_ids_survive_reboot(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address;uint32_t first,last;
    CHECK(store.load(cal,address));CHECK(store.reserveRequestIds(first,last)&&first==1&&last==65536);
    a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));
    CHECK(reboot.reserveRequestIds(first,last)&&first==65537&&last==131072);
    CHECK(reboot.initialize(cal,address)); // Factory calibration reset must not reset IDs.
    a2d::SettingsStore again;CHECK(again.load(cal,address));
    CHECK(again.reserveRequestIds(first,last)&&first==131073);
}
void request_reservations_survive_corruption_and_torn_mirror(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address;uint32_t first,last;
    CHECK(store.load(cal,address));CHECK(store.reserveRequestIds(first,last)&&first==1);
    uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    for(unsigned page=0;page<2;++page){
        memcpy(flash(),baseline,2048);flash()[page*1024+8]^=1;
        a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));
        CHECK(reboot.reserveRequestIds(first,last)&&first>=65537); // ID1 cannot be reused through either fallback.
    }
    for(int cut=0;cut<=58;++cut){
        memcpy(flash(),baseline,2048);failAfter=-1;
        a2d::SettingsStore before;CHECK(before.load(cal,address));failAfter=cut;
        const bool reserved=before.reserveRequestIds(first,last);failAfter=-1;
        const uint32_t highestIssued=reserved?first:1;
        a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));
        CHECK(reboot.reserveRequestIds(first,last)&&first>highestIssued);
    }
}
void repairTestRecordCrc(uint8_t *record){
    uint32_t crc=0xffffffffUL;
    for(unsigned i=0;i<48;++i){crc^=record[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320UL:0);}
    crc=~crc;memcpy(record+48,&crc,4);
}
void stored_calibration_range_repair_preserves_ids(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address;uint32_t first,last;
    CHECK(store.load(cal,address));CHECK(cal.calibrate(1,0,0,2.75f,1));
    CHECK(store.saveAllCalibration(cal)&&store.saveAddress(7));
    CHECK(store.reserveRequestIds(first,last)&&first==1&&last==65536);
    uint8_t baseline[2048];memcpy(baseline,flash(),2048);
    // Simulate CRC-valid records saved by an older image, not arbitrary corrupt metadata.
    for(int cut=0;cut<=29;++cut){
        memcpy(flash(),baseline,2048);
        for(unsigned page=0;page<2;++page){float huge=1e20f;memcpy(flash()+1024*page+8,&huge,4);repairTestRecordCrc(flash()+1024*page);}
        failAfter=cut;a2d::SettingsStore reboot;a2d::Calibration loaded;
        const bool ok=reboot.load(loaded,address);failAfter=-1;
        CHECK(ok==(cut>=29));CHECK(address==7);
        CHECK(loaded.get(0).gain==a2d::CalibrationLimits::DefaultGain&&loaded.get(1).gain==2.75f);
        a2d::SettingsStore retry;a2d::Calibration retried;CHECK(retry.load(retried,address));
        CHECK(address==7&&retried.get(0).gain==a2d::CalibrationLimits::DefaultGain&&retried.get(1).gain==2.75f);
        CHECK(retry.reserveRequestIds(first,last)&&first>=65537);
    }
}
void request_id_exhaustion_and_sequence_wrap(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address;uint32_t first,last;
    CHECK(store.load(cal,address));
    uint32_t next=0xfffffff0UL,sequence=0xffffffffUL;memcpy(flash()+44,&next,4);memcpy(flash()+4,&sequence,4);repairTestRecordCrc(flash());
    a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));
    CHECK(reboot.reserveRequestIds(first,last)&&first==0xfffffff0UL&&last==0xffffffffUL);
    a2d::SettingsStore wrapped;CHECK(wrapped.load(cal,address));CHECK(!wrapped.reserveRequestIds(first,last));
    CHECK(wrapped.initialize(cal,address));CHECK(!wrapped.reserveRequestIds(first,last));
}
void storage_corrupt_record_recovery(){
    cleanFlash();a2d::Calibration cal;a2d::SettingsStore store;uint8_t address;
    CHECK(store.load(cal,address));cal.calibrate(0,0,0,2.75f,1);CHECK(store.saveAllCalibration(cal));
    flash()[1024+8]^=1; // Corrupt newest copy; CRC rejects it and prior copy remains usable.
    a2d::SettingsStore reboot;CHECK(reboot.load(cal,address));CHECK(approx(cal.get(0).gain,2.818181818f));
    flash()[8]^=1; // Both invalid: initialize finite nominal defaults, never load NaN/address255.
    a2d::SettingsStore empty;CHECK(empty.load(cal,address)&&address==0);
    for(uint8_t ch=0;ch<4;++ch)CHECK(isfinite(cal.get(ch).gain)&&cal.get(ch).gain!=0);
    uint32_t first,last;CHECK(!empty.reserveRequestIds(first,last));
}
void board_reset_and_storage_errors(){
    cleanFlash();TwoWire bus;A2D_4CH_Isolated_ADC board;CHECK(board.init(&bus));
    CHECK(pin_values[PC13]==1 && pin_values[PB1]==0);
    CHECK(board.try_calibrate_voltage(0,0.25f,0.125f,4.375f,1.625f));const float gain=board.get_cal_gain(0),offset=board.get_cal_offset(0);
    CHECK(board.save_all_calibration());board.set_rs485_addr(7);CHECK(board.save_rs485_addr());
    board.reset_calibration(0);board.set_rs485_addr(1);CHECK(board.reset());
    CHECK(board.get_rs485_addr()==7 && board.get_cal_gain(0)==gain && board.get_cal_offset(0)==offset);
    bus.codes[0]=16384;CHECK(approx(board.measure_voltage(0),(1.024f-offset)*gain));
    bus.responseCount=0;CHECK(isnan(board.measure_voltage(0)));bus.responseCount=3;
    CHECK(board.try_calibrate_voltage(0,0,0,3,1));failAfter=0;CHECK(!board.save_all_calibration());failAfter=-1;
    CHECK(board.reset() && board.get_cal_gain(0)==gain);
    board.set_rs485_receive(false);CHECK(pin_values[PB1]==1);board.set_rs485_receive(true);CHECK(pin_values[PB1]==0);
}
int main(){
    CHECK(VirtualAlloc(reinterpret_cast<void *>(0x08000000),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reinterpret_cast<void *>(0x08000000));
    const struct{const char *name;void(*run)();} tests[]={
        {"calibration math",calibration_math},{"calibration input and coefficient ranges",calibration_ranges},
        {"stored calibration range repair preserves IDs",stored_calibration_range_repair_preserves_ids},
        {"ADC scaling and trigger shadows",adc_scaling_and_configuration},
        {"ADC failure and freshness",adc_failure_and_freshness},{"settings batching and save boundaries",storage_save_boundaries},
        {"power loss at every flash operation",storage_power_loss_every_operation},{"corrupt-record recovery",storage_corrupt_record_recovery},{"request IDs survive reboot",request_ids_survive_reboot},{"ID exhaustion and generation wrap",request_id_exhaustion_and_sequence_wrap},{"ID fallback and interrupted mirror",request_reservations_survive_corruption_and_torn_mirror},
        {"board persistence and error propagation",board_reset_and_storage_errors}};
    for(const auto &t:tests){try{t.run();}catch(const std::exception &e){std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<'\n';return 1;}std::cout<<"PASS "<<t.name<<'\n';}
}
