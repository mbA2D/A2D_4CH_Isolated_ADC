#include <iostream>
#include <stdexcept>
#include <new>
#include "fakes/Arduino.h"
unsigned long test_clock=0; Stream Serial; TwoWire Wire;
uint32_t test_usb_session=0;unsigned boot_requests=0;
void A2D_install_usb_session_hooks() {}
uint32_t A2D_usb_session_generation(){return test_usb_session;}
bool A2D_usb_session_open(){return Serial.connected;}
void A2D_request_usb_bootloader_reset(){++boot_requests;}
#include "../firmware/main.cpp"
#define CHECK(c) do{if(!(c))throw std::runtime_error(#c);}while(0)
const std::string identity="A2D Electronics,4CH Isolated ADC,0002,V2.1.3\r\n";
void reset_test(){
    Serial.clear();Serial3.clear();test_usb_session=0;boot_requests=0;g_a2d_adc=A2D_4CH_Isolated_ADC();
    g_transport.~Transport();new(&g_transport)a2d::Transport(Serial,Serial3,g_a2d_adc);test_clock=0;setup();
}
void usb(const std::string &text){
    Serial.input+=text;
    for(unsigned i=0;(Serial.available()||g_transport.hasBufferedInput(a2d::Source::Usb)||g_transport.busy())&&i<3000;++i)loop();
    CHECK(!Serial.available()&&!g_transport.busy());
}
void rs485(const std::string &text){
    Serial3.input+=text;
    for(unsigned i=0;(Serial3.available()||g_transport.hasBufferedInput(a2d::Source::Rs485))&&i<100;++i)loop();
    CHECK(!Serial3.available());
}
std::string lastHeader(){
    const auto end=Serial3.output.size();CHECK(end>2);
    const auto previous=Serial3.output.rfind('\n',end-2);const size_t first=previous==std::string::npos?0:previous+1;
    const auto a=Serial3.output.find(' ',first),b=Serial3.output.find(' ',a+1);CHECK(b!=std::string::npos);
    return Serial3.output.substr(first,b-first+1);
}
void respondWith(const std::string &value){Serial3.on_flush=[value]{Serial3.input=lastHeader()+value;};}
std::string withoutId(){const auto a=Serial3.output.find(' '),b=Serial3.output.find(' ',a+1);return Serial3.output.substr(0,a+1)+Serial3.output.substr(b+1);}
void identity_and_measurement(){usb("*IDN?\r\nMEAS:VOLT 1?\n");CHECK(Serial.output==identity+"1.0000\r\n");}
void partial_usb_services_rs485(){usb("INSTR:LED 1");rs485("0 123 MEAS:VOLT 1?\n");CHECK(Serial3.output=="0 123 1.0000\r\n");CHECK(!g_a2d_adc.led);usb("\n");CHECK(g_a2d_adc.led);}
void line_boundaries_and_recovery(){
    usb(std::string(200,'X')+"\n*IDN?\n");CHECK(Serial.output=="ERROR:LINE TOO LONG\r\n"+identity);
    Serial.output.clear();usb(std::string(199,'X')+"\n");CHECK(Serial.output=="ERROR:INVALID COMMAND\r\n");
    Serial.output.clear();usb(std::string("INSTR:LED 1\0junk\n",17));CHECK(!g_a2d_adc.led&&Serial.output=="ERROR:LINE TOO LONG\r\n");
}
void invalid_arguments_do_not_mutate(){
    for(const char *line:{"INSTR:LED","INSTR:LED -1","INSTR:LED 256","INSTR:LED 1 extra","MEAS:VOLT","MEAS:VOLT -1?","MEAS:VOLT 129?","INSTR:GAIN 3,0","INSTR:DR 256,0","INSTR:MODE 2,0","INSTR:RS485:SET 32","CAL:VOLT 1,0,nan,5,1","CAL:VOLT 1,0,inf,5,1","CAL:VOLT 1,0,1,5,1"}){
        Serial.output.clear();usb(std::string(line)+"\n");CHECK(Serial.output=="ERROR:INVALID ARGUMENTS\r\n");CHECK(!g_a2d_adc.led&&g_a2d_adc.address==0&&g_a2d_adc.calibration_calls==0);
    }
}
void all_channel_configuration(){usb("INSTR:GAIN 4,0\nINSTR:DR 15,0\nINSTR:MODE 1,0\n");for(unsigned i=0;i<4;++i)CHECK(g_a2d_adc.gains[i]==2&&g_a2d_adc.rates[i]==2&&g_a2d_adc.modes[i]==1);CHECK(Serial.output.empty());}
void forwarded_queries_and_writes(){
    respondWith("3.1415\r\n");usb("MEAS:VOLT 5?\n");CHECK(withoutId()=="1 MEAS:VOLT 5?\r\n"&&Serial.output=="3.1415\r\n"&&g_a2d_adc.receive);
    Serial3.clear();Serial.output.clear();usb("INSTR:GAIN 4,9\n");CHECK(withoutId()=="2 INSTR:GAIN 4,9\r\n"&&Serial.output.empty());
    Serial3.clear();usb("CAL:VOLT 5,0.1,0.05,4.9,1.75\n");CHECK(withoutId()=="1 CAL:VOLT 5,0.1,0.05,4.9,1.75\r\n");
}
void missing_or_unterminated_peer(){
    usb("MEAS:VOLT 5?\n");CHECK(Serial.output=="ERROR:RS485 TIMEOUT\r\n");CHECK(test_clock>=1000&&test_clock<1010);
    Serial.output.clear();respondWith("3.1415");usb("MEAS:VOLT 5?\n");CHECK(Serial.output=="ERROR:RS485 TIMEOUT\r\n");
    usb("*IDN?\n");CHECK(Serial.output.find(identity)!=std::string::npos);
}
void oversized_untrusted_peer_is_discarded(){respondWith(std::string(200,'1')+"\n");usb("MEAS:VOLT 5?\n");CHECK(Serial.output=="ERROR:RS485 TIMEOUT\r\n");}
void timeout_wrap(){test_clock=0xfffffff0UL;usb("MEAS:VOLT 5?\n");CHECK(Serial.output=="ERROR:RS485 TIMEOUT\r\n"&&static_cast<uint32_t>(test_clock)<1100);}
void addressed_peer_and_foreign_silence(){
    g_a2d_adc.address=1;rs485("1 123 MEAS:VOLT 5?\n");CHECK(Serial3.output=="1 123 1.0000\r\n"&&Serial.output.empty());
    Serial3.output.clear();rs485("2 124 INSTR:LED 1\n");CHECK(!g_a2d_adc.led&&Serial3.output.empty());
    rs485("1 125 MEAS:VOLT 9?\n");CHECK(Serial3.output=="1 125 ERROR:INVALID ARGUMENTS\r\n");
}
void last_channel_and_identity(){g_a2d_adc.address=31;rs485("31 400 MEAS:VOLT 128?\n31 401 *IDN?\n");CHECK(Serial3.output=="31 400 4.0000\r\n31 401 "+identity);}
void malformed_envelopes_stay_silent(){
    for(const char *frame:{"0 *IDN?\n","0 0 *IDN?\n","0 4294967296 *IDN?\n","32 1 *IDN?\n","0 -1 *IDN?\n","0 1\n"})rs485(frame);
    rs485(std::string(200,'X')+"\n");CHECK(Serial3.output.empty()&&Serial.output.empty());
}
void forwarding_boundaries(){
    for(const auto &test:{std::make_pair(5,185),std::make_pair(41,184)}){
        Serial.clear();Serial3.clear();g_transport.reset();
        const std::string command="MEAS:VOLT "+std::to_string(test.first)+"?";
        const std::string line=command+std::string(test.second-command.size(),' ');
        respondWith("1.2345\r\n");usb(line+"\n");CHECK(Serial.output=="1.2345\r\n"&&Serial3.output.size()<=200);
        Serial.clear();Serial3.clear();usb(line+" \n");CHECK(Serial.output=="ERROR:LINE TOO LONG\r\n"&&Serial3.output.empty());
    }
}
void calibration_overflow_before_routing(){for(const char *ch:{"1","5"}){Serial.output.clear();usb(std::string("CAL:VOLT ")+ch+",0,0,1e30,1e-30\n");CHECK(Serial.output=="ERROR:INVALID ARGUMENTS\r\n"&&Serial3.output.empty()&&g_a2d_adc.calibration_calls==0);}}
void nonblocking_and_ordered_forwarding(){
    Serial.input="MEAS:VOLT 5?\n*IDN?\n";loop();CHECK(g_transport.busy()&&test_clock<10&&Serial.output.empty());
    const auto header=lastHeader();for(unsigned i=0;i<10;++i)loop();CHECK(g_transport.busy());
    Serial3.input=header+"2.0000\r\n";loop();CHECK(Serial.output=="2.0000\r\n"&&!g_transport.busy());loop();CHECK(Serial.output=="2.0000\r\n"+identity);
}
void stale_wrong_address_and_wrong_id_rejected(){
    usb("MEAS:VOLT 5?\n");const auto oldHeader=lastHeader();Serial.output.clear();Serial3.output.clear();
    Serial.input="MEAS:VOLT 9?\n";loop();const auto currentHeader=lastHeader();
    Serial3.input=oldHeader+"1.2345\n";loop();CHECK(g_transport.busy()&&Serial.output.empty());
    Serial3.input="2 999 2.5000\n";loop();CHECK(g_transport.busy()&&Serial.output.empty());
    Serial3.input="3 2 2.5000\n";loop();CHECK(g_transport.busy()&&Serial.output.empty());
    Serial3.input=currentHeader+"3.0000\n";loop();CHECK(!g_transport.busy()&&Serial.output=="3.0000\r\n");
    // A stale reply from the same address is rejected as well.
    Serial.output.clear();Serial3.output.clear();Serial.input="MEAS:VOLT 9?\n";loop();const auto nextHeader=lastHeader();
    Serial3.input=currentHeader+"3.0000\n";loop();CHECK(g_transport.busy()&&Serial.output.empty());
    Serial3.input=nextHeader+"4.0000\n";loop();CHECK(Serial.output=="4.0000\r\n");
}
void stale_reply_from_previous_boot_is_rejected(){
    Serial.input="MEAS:VOLT 5?\n";loop();const auto previous=lastHeader();
    g_transport.~Transport();new(&g_transport)a2d::Transport(Serial,Serial3,g_a2d_adc);
    Serial3.output.clear();Serial.input="MEAS:VOLT 6?\n";loop();const auto current=lastHeader();
    CHECK(previous!=current);
    Serial3.input=previous+"1.2345\n";loop();CHECK(g_transport.busy()&&Serial.output.empty());
    Serial3.input=current+"2.3456\n";loop();CHECK(Serial.output=="2.3456\r\n");
}
void remaining_commands_and_point_order(){
    usb("MEAS:VOLT 0?\nMEAS:VOLT:ADC 0?\nCAL 0?\n");
    CHECK(Serial.output=="1.0000,2.0000,3.0000,4.0000\r\n0.1000,0.2000,0.3000,0.4000\r\n0.00000,1.00000,0.00000,1.00000,0.00000,1.00000,0.00000,1.00000\r\n");
    Serial.output.clear();usb("*CLS\n*TRG\n*RST\nCAL:RESET 2\nCAL:SAVE 3\nCAL:VOLT 4,0.25,0.125,4.375,1.625\nINSTR:RS485:SET 7\nINSTR:RS485:SAVE\nINSTR:RS485?\n");
    CHECK(Serial.output=="7\r\n"&&g_a2d_adc.trigger_calls==1&&g_a2d_adc.reset_calls==1);
    CHECK(g_a2d_adc.reset_mask==2&&g_a2d_adc.save_mask==4&&g_a2d_adc.calibration_channel==3&&g_a2d_adc.address_saves==1);
    CHECK(g_a2d_adc.calibration_points[0]==0.25f&&g_a2d_adc.calibration_points[1]==0.125f&&g_a2d_adc.calibration_points[2]==4.375f&&g_a2d_adc.calibration_points[3]==1.625f);
}
void owned_request_and_rs485_groups(){
    char input[]="CAL:VOLT 4,0.25,0.125,4.375,1.625";a2d::Request r;
    CHECK(a2d::parse(input,a2d::Source::Usb,0,r)==a2d::ParseResult::Ready);input[0]='X';
    CHECK(!strcmp(r.original,"CAL:VOLT 4,0.25,0.125,4.375,1.625")&&r.points[0]==0.25f&&r.points[1]==0.125f&&r.points[2]==4.375f&&r.points[3]==1.625f);
    rs485("0 1 MEAS:VOLT 0?\n0 2 CAL 0?\n0 3 CAL:RESET 0\n0 4 CAL:SAVE 0\n0 5 INSTR:GAIN 4,0\n");
    CHECK(Serial3.output.empty()&&Serial.output.empty()&&g_a2d_adc.reset_mask==0&&g_a2d_adc.save_mask==0);
    for(const auto gain:g_a2d_adc.gains)CHECK(gain==2);
}
void setters_with_question_mark_are_rejected(){
    for(const char *cmd:{"CAL:RESET 1?","CAL:SAVE 0?","INSTR:RS485:SET 7?","INSTR:LED 1?","INSTR:GAIN 2,0?","INSTR:MODE 0,0?","CAL:VOLT 1,0,0,2.75,1?","MEAS:VOLT 1??","MEAS:VOLT ?1"}){
        Serial.output.clear();usb(std::string(cmd)+"\n");CHECK(Serial.output=="ERROR:INVALID ARGUMENTS\r\n");
    }
    CHECK(g_a2d_adc.address==0&&!g_a2d_adc.led&&!g_a2d_adc.reset_mask&&!g_a2d_adc.save_mask&&!g_a2d_adc.calibration_calls);
}
void failure_responses_are_atomic(){
    g_a2d_adc.failing_channel=2;usb("MEAS:VOLT 0?\nMEAS:VOLT:ADC 3?\n");CHECK(Serial.output=="ERROR:ADC READ\r\nERROR:ADC READ\r\n");
    g_a2d_adc.io_ok=false;Serial.output.clear();usb("INSTR:GAIN 2,0\n*TRG\n");CHECK(Serial.output=="ERROR:ADC CONFIG\r\nERROR:ADC CONFIG\r\n");
    g_a2d_adc.storage_ok=false;Serial.output.clear();usb("CAL:SAVE 0\nINSTR:RS485:SAVE\n*RST\n");CHECK(Serial.output=="ERROR:STORAGE WRITE\r\nERROR:STORAGE WRITE\r\nERROR:RESET FAILED\r\n");
}
void usb_backpressure_disconnect_and_recovery(){
    unsigned flushCalls=0;Serial.on_flush=[&flushCalls]{++flushCalls;};Serial.space=0;usb("*IDN?\n");
    CHECK(Serial.output.empty()&&test_clock>=100&&test_clock<110&&flushCalls==0);
    Serial.space=127;usb("*IDN?\n");CHECK(Serial.output==identity&&flushCalls==0);
    Serial.output.clear();Serial.connected=false;const auto before=test_clock;g_transport.reply(a2d::Source::Usb,"test");CHECK(Serial.output.empty()&&test_clock-before<5);
    Serial.connected=true;usb("*IDN?\n");CHECK(Serial.output==identity);
    Serial.output.clear();Serial.space=10;usb("*IDN?\n");CHECK(Serial.output.empty()); // Full-line enqueue avoids fragments.
    Serial.space=127;Serial.spaceAfterWrite=0;Serial.output.clear();const auto queuedAt=test_clock;
    usb("*IDN?\n");CHECK(Serial.output==identity&&test_clock-queuedAt>=100&&test_clock-queuedAt<110&&flushCalls==0);
    Serial.spaceAfterWrite=-1;Serial.space=127;Serial.output.clear();usb("*IDN?\n");CHECK(Serial.output==identity);
}
void continuous_input_yields(){
    struct ContinuousPort{unsigned reads=0;int available(){return 64;}size_t readBytes(char *p,size_t n){if(++reads>4)throw std::runtime_error("unbounded receive");memset(p,'X',n);return n;}}port;
    a2d_scpi::LineReader<200> reader;char line[200];for(unsigned i=1;i<=4;++i)CHECK(reader.poll(port,line)==0&&port.reads==i);
    Serial.input=std::string(10000,'X');Serial3.input="0 1 MEAS:VOLT 1?\n";loop();CHECK(Serial.input.size()==9936&&Serial3.output=="0 1 1.0000\r\n");
    for(unsigned i=0;i<3;++i)loop();Serial.input="\n*IDN?\n";usb("");CHECK(Serial.output=="ERROR:LINE TOO LONG\r\n"+identity);
}
void numeric_output_is_atomic_and_representable(){
    for(float value:{1e20f,-1e20f}){
        g_a2d_adc.voltage_scale=value;Serial.output.clear();usb("MEAS:VOLT 0?\n");CHECK(Serial.output=="ERROR:NUMERIC RANGE\r\n");
        g_a2d_adc.cal_gain=value;Serial.output.clear();usb("CAL 0?\n");CHECK(Serial.output=="ERROR:NUMERIC RANGE\r\n");
        g_a2d_adc.cal_gain=1;g_a2d_adc.cal_offset=value;Serial.output.clear();usb("CAL 1?\n");CHECK(Serial.output=="ERROR:NUMERIC RANGE\r\n");
        g_a2d_adc.cal_offset=0;
    }
    g_a2d_adc.voltage_scale=4294967040.0f;Serial.output.clear();usb("MEAS:VOLT 1?\n");CHECK(Serial.output=="4294967040.0000\r\n");
    g_a2d_adc.voltage_scale=-4294967040.0f;Serial.output.clear();usb("MEAS:VOLT 1?\n");CHECK(Serial.output=="-4294967040.0000\r\n");
    g_a2d_adc.voltage_scale=1;Serial.output.clear();usb("MEAS:VOLT 0?\n*IDN?\n");CHECK(Serial.output=="1.0000,2.0000,3.0000,4.0000\r\n"+identity);
}
void command_ranges_reject_before_mutation_or_forwarding(){
    for(const char *channel:{"1","5","128"})for(const char *points:{
        "-0.001,0,5,2","0,0,5.001,2","0,0,1e20,1","0,0,0.0014,0.0005",
        "0,0,2,1","0,0,4,1","0,0,2.5,1","0,0,3.2,1",
        "0,1,2.5,0","0,0.2501,2.75,1.2501","0,0.23,5,2.0481"}){
        Serial.output.clear();Serial3.clear();
        usb(std::string("CAL:VOLT ")+channel+","+points+"\n");
        CHECK(Serial.output=="ERROR:INVALID ARGUMENTS\r\n"&&Serial3.output.empty()&&g_a2d_adc.calibration_calls==0);
    }
    for(const char *line:{"INSTR:LED 2","INSTR:LED -1","INSTR:GAIN 0,0","INSTR:GAIN 3,0","INSTR:GAIN 9,0",
        "INSTR:DR 0,0","INSTR:DR 14,0","INSTR:DR 241,0","INSTR:MODE 2,0","INSTR:RS485:SET 32",
        "MEAS:VOLT 129?","CAL:RESET 129","CAL:SAVE -1","CAL:VOLT 0,0,0,2.75,1"}){
        Serial.output.clear();Serial3.clear();usb(std::string(line)+"\n");
        CHECK(Serial.output=="ERROR:INVALID ARGUMENTS\r\n"&&Serial3.output.empty());
    }
    CHECK(g_a2d_adc.address==0&&!g_a2d_adc.led&&!g_a2d_adc.reset_mask&&!g_a2d_adc.save_mask&&!g_a2d_adc.calibration_calls);
    for(uint8_t value:g_a2d_adc.gains)CHECK(value==0);
    for(uint8_t value:g_a2d_adc.rates)CHECK(value==0);
    for(uint8_t value:g_a2d_adc.modes)CHECK(value==0);
    a2d::Request request;
    for(const char *line:{"INSTR:LED 0","INSTR:LED 1","INSTR:GAIN 1,0","INSTR:GAIN 8,128",
        "INSTR:DR 15,0","INSTR:DR 240,128","INSTR:MODE 0,0","INSTR:MODE 1,128",
        "INSTR:RS485:SET 0","INSTR:RS485:SET 31","MEAS:VOLT 0?","MEAS:VOLT 128?","CAL:SAVE 0"})
        CHECK(a2d::parse(line,a2d::Source::Usb,0,request)==a2d::ParseResult::Ready);
}
void usb_bootloader_requires_a_complete_usb_command(){
    for(const char *line:{"SYST:BOOT?","SYST:BOOT 1","SYST:BOOT EXTRA","SYST:BOOT;*IDN?","1EAF","1EAF_NOT_A_RESET"}){
        Serial.output.clear();usb(std::string(line)+"\n");CHECK(!boot_requests&&Serial.output=="ERROR:INVALID ARGUMENTS\r\n");
    }
    usb("SYST:BO");CHECK(!boot_requests);usb("OT\n");CHECK(boot_requests==1);
    rs485("0 123 SYST:BOOT\n");CHECK(boot_requests==1&&Serial3.output=="0 123 ERROR:INVALID ARGUMENTS\r\n");
    a2d::Request request={};request.command=a2d::Command::Bootloader;request.source=a2d::Source::Rs485;
    g_dispatcher.execute(request);CHECK(boot_requests==1);
    usb(std::string(200,'X')+"SYST:BOOT\n");CHECK(boot_requests==1);
}
void usb_session_discards_partial_and_overflow_frames(){
    g_a2d_adc.address=7;usb("INSTR:RS485:SET 3");
    test_usb_session+=2;usb("1\nINSTR:RS485?\n");
    CHECK(g_a2d_adc.address==7&&Serial.output=="ERROR:INVALID ARGUMENTS\r\n7\r\n");
    Serial.output.clear();usb(std::string(200,'X'));test_usb_session+=2;usb("*IDN?\n");CHECK(Serial.output==identity);
    Serial.output.clear();usb("SYST:BO");test_usb_session+=2;usb("OT\n");CHECK(!boot_requests&&Serial.output=="ERROR:INVALID ARGUMENTS\r\n");
}
void usb_session_cancels_forward_without_reusing_ids(){
    Serial.input="MEAS:VOLT 5?\n";loop();CHECK(g_transport.busy());
    const std::string previous=lastHeader();test_usb_session+=2;
    usb("*IDN?\n");CHECK(!g_transport.busy()&&Serial.output==identity&&test_clock<1000);
    Serial.output.clear();Serial3.on_flush=[previous]{Serial3.input=previous+"old\n"+lastHeader()+"new\n";};
    usb("MEAS:VOLT 5?\n");CHECK(Serial.output=="new\r\n"&&lastHeader()!=previous);
}
int main(){
    const struct{const char *name;void(*run)();} tests[]={
#define TEST(n) {#n,n}
        TEST(identity_and_measurement),TEST(partial_usb_services_rs485),TEST(line_boundaries_and_recovery),TEST(invalid_arguments_do_not_mutate),
        TEST(all_channel_configuration),TEST(forwarded_queries_and_writes),TEST(missing_or_unterminated_peer),TEST(oversized_untrusted_peer_is_discarded),
        TEST(timeout_wrap),TEST(addressed_peer_and_foreign_silence),TEST(last_channel_and_identity),TEST(malformed_envelopes_stay_silent),
        TEST(forwarding_boundaries),TEST(calibration_overflow_before_routing),TEST(nonblocking_and_ordered_forwarding),TEST(stale_wrong_address_and_wrong_id_rejected),
        TEST(stale_reply_from_previous_boot_is_rejected),TEST(remaining_commands_and_point_order),TEST(owned_request_and_rs485_groups),TEST(setters_with_question_mark_are_rejected),TEST(failure_responses_are_atomic),
        TEST(usb_backpressure_disconnect_and_recovery),TEST(continuous_input_yields),TEST(numeric_output_is_atomic_and_representable),TEST(command_ranges_reject_before_mutation_or_forwarding),
        TEST(usb_bootloader_requires_a_complete_usb_command),TEST(usb_session_discards_partial_and_overflow_frames),TEST(usb_session_cancels_forward_without_reusing_ids)};
    for(const auto &t:tests){reset_test();try{t.run();}catch(const std::exception &e){std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<'\n';return 1;}std::cout<<"PASS "<<t.name<<'\n';}
    std::cout<<sizeof(tests)/sizeof(tests[0])<<" application groups passed\n";
}
