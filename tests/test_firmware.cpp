#include <iostream>
#include <stdexcept>
#include "fakes/Arduino.h"
unsigned long test_clock=0; Stream Serial; TwoWire Wire;
uint32_t test_usb_session=0;unsigned boot_requests=0;
extern "C" uint32_t A2D_millis(){return test_clock++;}
extern "C" void A2D_delay(uint32_t ms){test_clock+=ms;}
extern "C" uint32_t A2D_irq_lock(){return 0;}
extern "C" void A2D_irq_unlock(uint32_t){}
extern "C" uint32_t A2D_usb_irq_lock(){return 0;}
extern "C" void A2D_usb_irq_unlock(uint32_t){}
extern "C" uint32_t A2D_usb_session_generation(){return test_usb_session;}
extern "C" bool A2D_usb_session_open(){return Serial.connected;}
extern "C" void A2D_request_usb_bootloader_reset(){++boot_requests;}
#include "A2D_Application.h"
HardwareSerial Serial3(11,10);
A2D_Board g_a2d_adc;
A2D_Transport g_transport;
void setup(){a2d_board_init(&g_a2d_adc,nullptr);}
void loop(){a2d_application_poll(&g_transport);}
#define CHECK(c) do{if(!(c))throw std::runtime_error(#c);}while(0)
const std::string identity="A2D Electronics,4CH Isolated ADC,0002,V5.0.1\r\n";
void reset_test(){
    Serial.clear();Serial3.clear();test_usb_session=0;boot_requests=0;a2d_fake_board_defaults(&g_a2d_adc);
    a2d_transport_init(&g_transport,&Serial.port,&Serial3.port,&g_a2d_adc);test_clock=0;setup();
}
void usb(const std::string &text){
    Serial.input+=text;
    for(unsigned i=0;(Serial.available()||a2d_transport_has_buffered_input(&g_transport,A2D_SOURCE_USB)||g_transport.waiting||g_transport.usb_pending_size)&&i<3000;++i)loop();
    CHECK(!Serial.available()&&!a2d_transport_has_buffered_input(&g_transport,A2D_SOURCE_USB)&&!g_transport.waiting&&!g_transport.usb_pending_size);
}
std::string pendingUsbReply(){return std::string(reinterpret_cast<const char *>(g_transport.usb_pending),g_transport.usb_pending_size);}
void rs485(const std::string &text){
    Serial3.input+=text;
    for(unsigned i=0;(Serial3.available()||a2d_transport_has_buffered_input(&g_transport,A2D_SOURCE_RS485))&&i<100;++i)loop();
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
void identity_and_measurement(){usb("*IDN?\r\nMEAS:VOLT? 1\n");CHECK(Serial.output==identity+"1.0000\r\n");}
void partial_usb_services_rs485(){usb("INSTR:LED 1");rs485("0 123 MEAS:VOLT? 1\n");CHECK(Serial3.output=="0 123 1.0000\r\n");CHECK(!g_a2d_adc.led);usb("\n");CHECK(g_a2d_adc.led);}
void line_boundaries_and_recovery(){
    usb(std::string(200,'X')+"\n*IDN?\n");CHECK(Serial.output==""+identity);
    Serial.output.clear();usb(std::string(199,'X')+"\n");CHECK(Serial.output=="");
    Serial.output.clear();usb(std::string("INSTR:LED 1\0junk\n",17));CHECK(!g_a2d_adc.led&&Serial.output=="");
}
void invalid_arguments_do_not_mutate(){
    for(const char *line:{"INSTR:LED","INSTR:LED -1","INSTR:LED 256","INSTR:LED 1 extra","MEAS:VOLT","MEAS:VOLT? -1","MEAS:VOLT? 129","INSTR:GAIN 3,0","INSTR:DR 256,0","INSTR:MODE 2,0","INSTR:RS485:SET 32","CAL:VOLT 1,0,nan,5,1","CAL:VOLT 1,0,inf,5,1","CAL:VOLT 1,0,1,5,1"}){
        Serial.output.clear();usb(std::string(line)+"\n");CHECK(Serial.output=="");CHECK(!g_a2d_adc.led&&g_a2d_adc.address==0&&g_a2d_adc.calibration_calls==0);
    }
}
void all_channel_configuration(){usb("INSTR:GAIN 4,0\nINSTR:DR 15,0\nINSTR:MODE 1,0\n");for(unsigned i=0;i<4;++i)CHECK(g_a2d_adc.gains[i]==2&&g_a2d_adc.rates[i]==2&&g_a2d_adc.modes[i]==1);CHECK(Serial.output.empty());}
void forwarded_queries_and_writes(){
    respondWith("3.1415\r\n");usb("MEAS:VOLT? 5\n");CHECK(withoutId()=="1 MEAS:VOLT? 5\r\n"&&Serial.output=="3.1415\r\n"&&g_a2d_adc.receive);
    Serial3.clear();Serial.output.clear();usb("INSTR:GAIN 4,9\n");CHECK(withoutId()=="2 INSTR:GAIN 4,9\r\n"&&Serial.output.empty());
    Serial3.clear();usb("CAL:VOLT 5,0.1,0.05,4.9,1.75\n");CHECK(withoutId()=="1 CAL:VOLT 5,0.1,0.05,4.9,1.75\r\n");
}
void missing_or_unterminated_peer(){
    usb("MEAS:VOLT? 5\n");CHECK(Serial.output=="");CHECK(test_clock>=1000&&test_clock<1010);
    Serial.output.clear();respondWith("3.1415");usb("MEAS:VOLT? 5\n");CHECK(Serial.output=="");
    usb("*IDN?\n");CHECK(Serial.output.find(identity)!=std::string::npos);
}
void oversized_untrusted_peer_is_discarded(){respondWith(std::string(200,'1')+"\n");usb("MEAS:VOLT? 5\n");CHECK(Serial.output=="");}
void timeout_wrap(){test_clock=0xfffffff0UL;usb("MEAS:VOLT? 5\n");CHECK(Serial.output==""&&static_cast<uint32_t>(test_clock)<1100);}
void addressed_peer_and_foreign_silence(){
    g_a2d_adc.address=1;rs485("1 123 MEAS:VOLT? 5\n");CHECK(Serial3.output=="1 123 1.0000\r\n"&&Serial.output.empty());
    Serial3.output.clear();rs485("2 124 INSTR:LED 1\n");CHECK(!g_a2d_adc.led&&Serial3.output.empty());
    rs485("1 125 MEAS:VOLT? 9\n");CHECK(Serial3.output=="");
}
void last_channel_and_identity(){g_a2d_adc.address=31;rs485("31 400 MEAS:VOLT? 128\n31 401 *IDN?\n");CHECK(Serial3.output=="31 400 4.0000\r\n31 401 "+identity);}
void malformed_envelopes_stay_silent(){
    for(const char *frame:{"0 *IDN?\n","0 0 *IDN?\n","0 4294967296 *IDN?\n","32 1 *IDN?\n","0 -1 *IDN?\n","0 1\n"})rs485(frame);
    rs485(std::string(200,'X')+"\n");CHECK(Serial3.output.empty()&&Serial.output.empty());
}
void forwarding_boundaries(){
    for(const auto &test:{std::make_pair(5,185),std::make_pair(41,184)}){
        Serial.clear();Serial3.clear();a2d_transport_reset(&g_transport);
        const std::string command="MEAS:VOLT? "+std::to_string(test.first);
        const std::string line=command+std::string(test.second-command.size(),' ');
        respondWith("1.2345\r\n");usb(line+"\n");CHECK(Serial.output=="1.2345\r\n"&&Serial3.output.size()<=200);
        Serial.clear();Serial3.clear();usb(line+" \n");CHECK(Serial.output==""&&Serial3.output.empty());
    }
}
void calibration_overflow_before_routing(){for(const char *ch:{"1","5"}){Serial.output.clear();usb(std::string("CAL:VOLT ")+ch+",0,0,1e30,1e-30\n");CHECK(Serial.output==""&&Serial3.output.empty()&&g_a2d_adc.calibration_calls==0);}}
void nonblocking_and_ordered_forwarding(){
    Serial.input="MEAS:VOLT? 5\n*IDN?\n";loop();CHECK(g_transport.waiting&&test_clock<10&&Serial.output.empty());
    const auto header=lastHeader();for(unsigned i=0;i<10;++i)loop();CHECK(g_transport.waiting);
    Serial3.input=header+"2.0000\r\n";loop();CHECK(Serial.output=="2.0000\r\n"&&!g_transport.waiting);loop();CHECK(Serial.output=="2.0000\r\n"+identity);
}
void stale_wrong_address_and_wrong_id_rejected(){
    usb("MEAS:VOLT? 5\n");const auto oldHeader=lastHeader();Serial.output.clear();Serial3.output.clear();
    Serial.input="MEAS:VOLT? 9\n";loop();const auto currentHeader=lastHeader();
    Serial3.input=oldHeader+"1.2345\n";loop();CHECK(g_transport.waiting&&Serial.output.empty());
    Serial3.input="2 999 2.5000\n";loop();CHECK(g_transport.waiting&&Serial.output.empty());
    Serial3.input="3 2 2.5000\n";loop();CHECK(g_transport.waiting&&Serial.output.empty());
    Serial3.input=currentHeader+"3.0000\n";loop();CHECK(!g_transport.waiting&&Serial.output=="3.0000\r\n");
    // A stale reply from the same address is rejected as well.
    Serial.output.clear();Serial3.output.clear();Serial.input="MEAS:VOLT? 9\n";loop();const auto nextHeader=lastHeader();
    Serial3.input=currentHeader+"3.0000\n";loop();CHECK(g_transport.waiting&&Serial.output.empty());
    Serial3.input=nextHeader+"4.0000\n";loop();CHECK(Serial.output=="4.0000\r\n");
}
void stale_reply_from_previous_boot_is_rejected(){
    Serial.input="MEAS:VOLT? 5\n";loop();const auto previous=lastHeader();
    a2d_transport_init(&g_transport,&Serial.port,&Serial3.port,&g_a2d_adc);
    Serial3.output.clear();Serial.input="MEAS:VOLT? 6\n";loop();const auto current=lastHeader();
    CHECK(previous!=current);
    Serial3.input=previous+"1.2345\n";loop();CHECK(g_transport.waiting&&Serial.output.empty());
    Serial3.input=current+"2.3456\n";loop();CHECK(Serial.output=="2.3456\r\n");
}
void remaining_commands_and_point_order(){
    usb("MEAS:VOLT? 0\nMEAS:VOLT:ADC? 0\nCAL? 0\n");
    CHECK(Serial.output=="1.0000,2.0000,3.0000,4.0000\r\n0.1000,0.2000,0.3000,0.4000\r\n0.00000,1.00000,0.00000,1.00000,0.00000,1.00000,0.00000,1.00000\r\n");
    Serial.output.clear();usb("*CLS\n*TRG\n*RST\nCAL:RESET 2\nCAL:SAVE 3\nCAL:VOLT 4,0.25,0.125,4.375,1.625\nINSTR:RS485:SET 7\nINSTR:RS485:SAVE\nINSTR:RS485?\n");
    CHECK(Serial.output=="7\r\n"&&g_a2d_adc.trigger_calls==1&&g_a2d_adc.reset_calls==1);
    CHECK(g_a2d_adc.reset_mask==2&&g_a2d_adc.save_mask==4&&g_a2d_adc.calibration_channel==3&&g_a2d_adc.address_saves==1);
    CHECK(g_a2d_adc.calibration_points[0]==0.25f&&g_a2d_adc.calibration_points[1]==0.125f&&g_a2d_adc.calibration_points[2]==4.375f&&g_a2d_adc.calibration_points[3]==1.625f);
}
void owned_request_and_rs485_groups(){
    char input[]="CAL:VOLT 4,0.25,0.125,4.375,1.625";A2D_Request r;
    CHECK(a2d_parse(input,A2D_SOURCE_USB,0,&r)==A2D_PARSE_READY);input[0]='X';
    CHECK(!strcmp(r.original,"CAL:VOLT 4,0.25,0.125,4.375,1.625")&&r.points[0]==0.25f&&r.points[1]==0.125f&&r.points[2]==4.375f&&r.points[3]==1.625f);
    rs485("0 1 MEAS:VOLT? 0\n0 2 CAL? 0\n0 3 CAL:RESET 0\n0 4 CAL:SAVE 0\n0 5 INSTR:GAIN 4,0\n");
    CHECK(Serial3.output.empty()&&Serial.output.empty()&&g_a2d_adc.reset_mask==0&&g_a2d_adc.save_mask==0);
    for(const auto gain:g_a2d_adc.gains)CHECK(gain==2);
}
void setters_with_question_mark_are_rejected(){
    for(const char *cmd:{"CAL:RESET 1?","CAL:SAVE 0?","INSTR:RS485:SET 7?","INSTR:LED 1?","INSTR:GAIN 2,0?","INSTR:MODE 0,0?","CAL:VOLT 1,0,0,2.75,1?","MEAS:VOLT? 1?","MEAS:VOLT ?1"}){
        Serial.output.clear();usb(std::string(cmd)+"\n");CHECK(Serial.output=="");
    }
    CHECK(g_a2d_adc.address==0&&!g_a2d_adc.led&&!g_a2d_adc.reset_mask&&!g_a2d_adc.save_mask&&!g_a2d_adc.calibration_calls);
}
void failure_responses_are_atomic(){
    g_a2d_adc.failing_channel=2;usb("MEAS:VOLT? 0\nMEAS:VOLT:ADC? 3\n");CHECK(Serial.output=="");
    g_a2d_adc.io_ok=false;Serial.output.clear();usb("INSTR:GAIN 2,0\n*TRG\n");CHECK(Serial.output=="");
    g_a2d_adc.storage_ok=false;Serial.output.clear();usb("CAL:SAVE 0\nINSTR:RS485:SAVE\n*RST\n");CHECK(Serial.output=="");
}
void usb_backpressure_disconnect_and_recovery(){
    unsigned flushCalls=0;Serial.on_flush=[&flushCalls]{++flushCalls;};Serial.space=0;Serial.input="*IDN?\n";
    const auto blockedAt=test_clock;loop();
    CHECK(Serial.output.empty()&&pendingUsbReply()==identity&&test_clock-blockedAt<5&&flushCalls==0);
    Serial.space=127;loop();CHECK(Serial.output==identity&&!g_transport.usb_pending_size&&flushCalls==0);
    Serial.output.clear();Serial.connected=false;const auto before=test_clock;a2d_transport_reply(&g_transport,A2D_SOURCE_USB,"test");CHECK(Serial.output.empty()&&test_clock-before<5);
    CHECK(!g_transport.usb_pending_size);
    Serial.connected=true;usb("*IDN?\n");CHECK(Serial.output==identity);
    Serial.output.clear();Serial.space=10;Serial.input="*IDN?\n";loop();CHECK(Serial.output.empty()&&pendingUsbReply()==identity); // Full-line enqueue avoids fragments.
    Serial.space=127;loop();CHECK(Serial.output==identity&&!g_transport.usb_pending_size);
    Serial.space=127;Serial.spaceAfterWrite=0;Serial.output.clear();const auto queuedAt=test_clock;
    usb("*IDN?\n");CHECK(Serial.output==identity&&test_clock-queuedAt<5&&!g_transport.usb_pending_size&&flushCalls==0);
    Serial.spaceAfterWrite=-1;Serial.space=127;Serial.output.clear();usb("*IDN?\n");CHECK(Serial.output==identity);
}
void usb_replies_are_ordered_under_backpressure(){
    Serial.space=0;Serial.input="MEAS:VOLT? 1\nINSTR:LED 1\nMEAS:VOLT? 2\n*IDN?\n";loop();
    CHECK(Serial.output.empty()&&pendingUsbReply()=="1.0000\r\n"&&!g_a2d_adc.led);
    const auto position=g_transport.usb_line.position,count=g_transport.usb_line.count;
    CHECK(a2d_transport_has_buffered_input(&g_transport,A2D_SOURCE_USB));
    test_clock+=10000;const auto blockedAt=test_clock;
    for(unsigned i=0;i<200;++i)loop();
    CHECK(Serial.output.empty()&&pendingUsbReply()=="1.0000\r\n"&&!g_a2d_adc.led&&test_clock==blockedAt);
    CHECK(g_transport.usb_line.position==position&&g_transport.usb_line.count==count);
    Serial.space=127;Serial.spaceAfterWrite=0;loop();loop();
    CHECK(Serial.output=="1.0000\r\n"&&g_a2d_adc.led&&pendingUsbReply()=="2.0000\r\n");
    for(unsigned i=0;i<20;++i)loop();
    CHECK(Serial.output=="1.0000\r\n"&&pendingUsbReply()=="2.0000\r\n");
    Serial.space=127;loop();
    CHECK(Serial.output=="1.0000\r\n2.0000\r\n"&&pendingUsbReply()==identity);
    Serial.space=127;loop();
    CHECK(Serial.output=="1.0000\r\n2.0000\r\n"+identity&&!g_transport.usb_pending_size);
    Serial.spaceAfterWrite=-1;Serial.space=127;usb("");
    CHECK(Serial.output=="1.0000\r\n2.0000\r\n"+identity);
}
void forwarded_replies_and_timeouts_are_ordered_under_backpressure(){
    for(bool peerResponds:{true,false}){
        reset_test();Serial.space=0;if(peerResponds)respondWith("2.5000\r\n");
        Serial.input="MEAS:VOLT? 5\nMEAS:VOLT? 1\n*IDN?\n";loop();CHECK(g_transport.waiting);
        for(unsigned i=0;g_transport.waiting&&i<1500;++i)loop();
        CHECK(!g_transport.waiting&&Serial.output.empty());
        if(peerResponds)CHECK(pendingUsbReply()=="2.5000\r\n");
        else {
            CHECK(!g_transport.usb_pending_size&&a2d_scpi_errors_peek(&g_transport.errors)==A2D_ERROR_RS485_TIMEOUT);
            loop();CHECK(pendingUsbReply()=="1.0000\r\n");
        }
        const auto first=pendingUsbReply();const auto position=g_transport.usb_line.position;
        test_clock+=10000;const auto blockedAt=test_clock;
        for(unsigned i=0;i<20;++i)loop();
        CHECK(Serial.output.empty()&&pendingUsbReply()==first&&g_transport.usb_line.position==position&&test_clock==blockedAt);
        Serial.space=127;usb("");
        CHECK(Serial.output==(peerResponds?"2.5000\r\n":"")+std::string("1.0000\r\n")+identity);
    }
}
void continuous_complete_usb_services_rs485(){
    g_a2d_adc.address=1;std::string expectedRs485;
    for(unsigned request=1;request<=1000;++request){
        const std::string header="1 "+std::to_string(request)+" ";
        Serial3.input+=header+"MEAS:VOLT? 5\n";expectedRs485+=header+"1.0000\r\n";
        for(unsigned turn=0;turn<2;++turn){
            // Keep complete USB commands ready on every call, including cached input.
            if(!Serial.available()&&!a2d_transport_has_buffered_input(&g_transport,A2D_SOURCE_USB))Serial.input="*IDN?\n*IDN?\n";
            loop();
        }
        CHECK(Serial3.output==expectedRs485&&!Serial3.available());
        CHECK(Serial.output.size()==request*identity.size()&&!g_transport.usb_pending_size);
    }
}
void pending_usb_reply_services_rs485(){
    g_a2d_adc.address=1;Serial.space=0;Serial.input="*IDN?\nINSTR:LED 1\n";loop();
    CHECK(pendingUsbReply()==identity&&Serial.output.empty()&&!g_a2d_adc.led);
    const auto position=g_transport.usb_line.position,count=g_transport.usb_line.count;
    std::string expectedRs485;
    for(unsigned request=1;request<=1000;++request){
        const std::string header="1 "+std::to_string(request)+" ";
        const unsigned kind=request%3;
        Serial3.input=header+(kind==0?"*IDN?\n":kind==1?"MEAS:VOLT? 5\n":"UNKNOWN\n");
        if(kind<2)expectedRs485+=header+(kind==0?identity:"1.0000\r\n");
        loop();
        CHECK(Serial3.output==expectedRs485&&!Serial3.available());
        CHECK(Serial.output.empty()&&pendingUsbReply()==identity&&!g_a2d_adc.led);
        CHECK(g_transport.usb_line.position==position&&g_transport.usb_line.count==count);
    }
    Serial.space=127;loop();CHECK(Serial.output==identity&&!g_transport.usb_pending_size&&g_a2d_adc.led);
}
void pending_usb_reply_is_purged_by_session_disconnect_and_reset(){
    Serial.space=0;Serial.input="*IDN?\nINSTR:LED 1\n";loop();CHECK(pendingUsbReply()==identity);
    test_usb_session+=2;Serial.space=127;Serial.input="MEAS:VOLT? 2\n";loop();
    CHECK(Serial.output=="2.0000\r\n"&&!g_transport.usb_pending_size&&!g_a2d_adc.led);
    Serial.output.clear();Serial.space=0;Serial.input="MEAS:VOLT? 1\n";loop();CHECK(pendingUsbReply()=="1.0000\r\n");
    a2d_transport_reset(&g_transport);CHECK(!g_transport.usb_pending_size);
    Serial.space=127;loop();CHECK(Serial.output.empty());
    Serial.space=0;Serial.input="MEAS:VOLT? 3\n";loop();CHECK(pendingUsbReply()=="3.0000\r\n");
    Serial.connected=false;loop();CHECK(!g_transport.usb_pending_size&&Serial.output.empty());
    Serial.connected=true;Serial.space=127;usb("*IDN?\n");CHECK(Serial.output==identity);
    Serial.output.clear();Serial.space=0;respondWith("2.5000\r\n");
    Serial.input="MEAS:VOLT? 5\n";loop();loop();CHECK(pendingUsbReply()=="2.5000\r\n");
    const auto previous=lastHeader();test_usb_session+=2;Serial.space=127;usb("*IDN?\n");
    CHECK(Serial.output==identity&&!g_transport.usb_pending_size);
    Serial.output.clear();Serial3.on_flush=[previous]{Serial3.input=previous+"old\n"+lastHeader()+"new\n";};
    usb("MEAS:VOLT? 5\n");CHECK(Serial.output=="new\r\n"&&lastHeader()!=previous);
}
void usb_pending_boundaries_and_failed_enqueue(){
    Serial.reject_write=true;Serial.input="MEAS:VOLT? 1\nMEAS:VOLT? 2\n";loop();
    CHECK(Serial.output.empty()&&pendingUsbReply()=="1.0000\r\n");
    for(unsigned i=0;i<10;++i)loop();CHECK(Serial.output.empty()&&pendingUsbReply()=="1.0000\r\n");
    Serial.reject_write=false;usb("");CHECK(Serial.output=="1.0000\r\n2.0000\r\n");
    Serial.output.clear();Serial.space=0;const std::string maximum(125,'X');
    a2d_transport_reply(&g_transport,A2D_SOURCE_USB,maximum.c_str());CHECK(g_transport.usb_pending_size==127&&pendingUsbReply()==maximum+"\r\n");
    Serial.space=126;loop();CHECK(Serial.output.empty()&&g_transport.usb_pending_size==127);
    Serial.space=127;loop();CHECK(Serial.output==maximum+"\r\n"&&!g_transport.usb_pending_size);
    Serial.output.clear();Serial.space=0;const std::string oversized(126,'X');
    a2d_transport_reply(&g_transport,A2D_SOURCE_USB,oversized.c_str());CHECK(pendingUsbReply()=="");
    Serial.space=127;loop();CHECK(Serial.output==""&&!g_transport.usb_pending_size);
}
void forward_drain_is_bounded_and_reports_busy(){
    for(bool query:{false,true}){
        reset_test();Serial3.endlessly_readable=true;Serial3.read_budget=512;
        Serial.input=query?"MEAS:VOLT? 5\n":"INSTR:GAIN 4,5\n";const auto before=test_clock;loop();
        CHECK(Serial3.read_bytes==512&&Serial3.read_calls==512&&Serial3.available()>0);
        CHECK(Serial.output==""&&Serial3.write_requests.empty()&&!g_transport.waiting&&g_a2d_adc.receive);
        CHECK(test_clock-before<5&&g_transport.next_id==0&&g_a2d_adc.next_reserved==1);
        Serial3.endlessly_readable=false;Serial3.read_budget=static_cast<size_t>(-1);usb("*IDN?\n");
        CHECK(Serial.output==""+identity&&Serial3.write_requests.empty());
    }
    reset_test();Serial3.input="stale";Serial3.reject_read=true;Serial.input="MEAS:VOLT? 5\n";loop();
    CHECK(Serial3.read_calls==1&&Serial3.read_bytes==0&&Serial3.input=="stale");
    CHECK(Serial.output==""&&Serial3.write_requests.empty()&&!g_transport.waiting&&g_a2d_adc.receive);
    for(unsigned count:{511,512,513}){
        reset_test();Serial3.input=std::string(count,'X');Serial.input="INSTR:GAIN 4,5\n";loop();
        CHECK(Serial3.read_bytes==(count>512?512:count)&&Serial3.input.size()==(count>512?count-512:0));
        if(count>512){
            CHECK(Serial.output==""&&Serial3.write_requests.empty()&&g_transport.next_id==0);
        }else{
            CHECK(Serial.output.empty()&&Serial3.write_requests.size()==1&&Serial3.write_requests[0]=="1 1 INSTR:GAIN 4,5\r\n");
        }
        CHECK(!g_transport.waiting&&g_a2d_adc.receive);
    }
}
void failed_forward_writes_stop_without_waiting_or_retry(){
    for(bool query:{false,true})for(int limit:{0,7}){
        reset_test();unsigned drains=0;
        Serial3.reject_write=limit==0;Serial3.write_limit=limit;
        Serial3.on_write=[]{CHECK(!g_a2d_adc.receive);};
        Serial3.on_flush=[&drains]{CHECK(!g_a2d_adc.receive);++drains;};
        const std::string command=query?"MEAS:VOLT? 5":"INSTR:GAIN 4,5";
        const std::string frame="1 1 "+command+"\r\n";
        Serial.input=command+"\n*IDN?\n";const auto before=test_clock;loop();
        CHECK(Serial.output==""&&!g_transport.waiting&&g_a2d_adc.receive&&test_clock-before<5);
        CHECK(Serial3.write_requests.size()==1&&Serial3.write_requests[0]==frame&&Serial3.output==frame.substr(0,limit)&&drains==1);
        for(unsigned i=0;i<20;++i)loop();test_clock+=2000;loop();
        CHECK(Serial.output==""+identity&&Serial3.write_requests.size()==1&&drains==1&&!g_transport.waiting);
        // A new explicit request may write again; do not assume a peer resynchronizes after a partial frame.
        Serial3.reject_write=false;Serial3.write_limit=-1;usb("INSTR:GAIN 4,5\n");
        CHECK(Serial.output==""+identity&&Serial3.write_requests.size()==2&&drains==2);
        CHECK(Serial3.write_requests[1]=="1 2 INSTR:GAIN 4,5\r\n"&&!g_transport.waiting&&g_a2d_adc.receive);
    }
}
void failed_forward_write_respects_usb_backpressure(){
    Serial.space=0;Serial3.reject_write=true;Serial.input="MEAS:VOLT? 5\n*IDN?\n";loop();
    CHECK(Serial.output.empty()&&!g_transport.usb_pending_size&&Serial3.write_requests.size()==1);
    CHECK(a2d_scpi_errors_peek(&g_transport.errors)==A2D_ERROR_RS485_WRITE&&!g_transport.waiting&&g_a2d_adc.receive);
    for(unsigned i=0;i<20;++i)loop();
    CHECK(pendingUsbReply()==identity&&Serial3.write_requests.size()==1);
    Serial.space=127;loop();CHECK(Serial.output==identity&&!g_transport.usb_pending_size);
}
void rs485_replies_are_single_checked_frames(){
    for(bool wrapper:{false,true})for(int limit:{-1,0,7}){
        reset_test();g_a2d_adc.address=31;Serial3.input="31 4294967295 *IDN?\n";char line[A2D_LINE_CAPACITY];
        CHECK(a2d_transport_poll(&g_transport,A2D_SOURCE_RS485,line)==1&&!strcmp(line,"*IDN?"));
        unsigned drains=0;Serial3.write_limit=limit;
        Serial3.on_write=[]{CHECK(!g_a2d_adc.receive);};
        Serial3.on_flush=[&drains]{CHECK(!g_a2d_adc.receive);++drains;};
        bool sent;
        if(wrapper)sent=a2d_transport_reply(&g_transport,A2D_SOURCE_RS485,"PAYLOAD");
        else{
            A2D_Writer writer=a2d_transport_begin_reply(&g_transport,A2D_SOURCE_RS485);
            a2d_writer_text(&writer,"PAYLOAD");a2d_writer_newline(&writer);
            sent=a2d_transport_end_reply(&g_transport,A2D_SOURCE_RS485);
        }
        const std::string frame="31 4294967295 PAYLOAD\r\n";
        CHECK(sent==(limit<0)&&Serial3.write_requests.size()==1&&Serial3.write_requests[0]==frame);
        CHECK(Serial3.output==(limit<0?frame:frame.substr(0,limit))&&drains==1&&g_a2d_adc.receive);
        for(unsigned i=0;i<10;++i)loop();CHECK(Serial3.write_requests.size()==1&&drains==1);
    }
    for(unsigned length:{184,185}){
        reset_test();g_transport.reply_address=31;g_transport.reply_id=0xffffffffUL;
        const std::string payload(length,'X');const bool sent=a2d_transport_reply(&g_transport,A2D_SOURCE_RS485,payload.c_str());
        CHECK(sent==(length==184)&&g_a2d_adc.receive);
        if(sent)CHECK(Serial3.write_requests.size()==1&&Serial3.write_requests[0]=="31 4294967295 "+payload+"\r\n"&&Serial3.output.size()==200);
        else CHECK(Serial3.write_requests.empty()&&Serial3.output.empty());
    }
}
void usb_reply_status_preserves_pending_output(){
    Serial.space=0;CHECK(a2d_transport_reply(&g_transport,A2D_SOURCE_USB,"first"));
    CHECK(pendingUsbReply()=="first\r\n"&&Serial.output.empty());
    CHECK(!a2d_transport_reply(&g_transport,A2D_SOURCE_USB,"second")&&pendingUsbReply()=="first\r\n");
    Serial.space=127;loop();CHECK(Serial.output=="first\r\n"&&!g_transport.usb_pending_size);
    Serial.space=0;A2D_Writer writer=a2d_transport_begin_reply(&g_transport,A2D_SOURCE_USB);
    a2d_writer_text(&writer,"third");a2d_writer_newline(&writer);CHECK(a2d_transport_end_reply(&g_transport,A2D_SOURCE_USB));
    writer=a2d_transport_begin_reply(&g_transport,A2D_SOURCE_USB);a2d_writer_text(&writer,"fourth");a2d_writer_newline(&writer);
    CHECK(!a2d_transport_end_reply(&g_transport,A2D_SOURCE_USB)&&pendingUsbReply()=="third\r\n");
    Serial.space=127;loop();CHECK(Serial.output=="first\r\nthird\r\n"&&!g_transport.usb_pending_size);
}
void continuous_input_yields(){
    struct ContinuousPort{unsigned reads=0;int available(){return 64;}size_t readBytes(char *p,size_t n){if(++reads>4)throw std::runtime_error("unbounded receive");memset(p,'X',n);return n;}}port;
    A2D_ScpiInput input={&port,
        [](void *context){return static_cast<ContinuousPort *>(context)->available();},
        [](void *context,char *data,size_t size){return static_cast<ContinuousPort *>(context)->readBytes(data,size);}};
    A2D_ScpiLineReader reader;char storage[200],line[200];
    CHECK(a2d_scpi_reader_init(&reader,storage,sizeof(storage)));
    for(unsigned i=1;i<=4;++i)CHECK(a2d_scpi_reader_poll(&reader,&input,line)==0&&port.reads==i);
    Serial.input=std::string(10000,'X');Serial3.input="0 1 MEAS:VOLT? 1\n";loop();CHECK(Serial.input.size()==9936&&Serial3.output=="0 1 1.0000\r\n");
    for(unsigned i=0;i<3;++i)loop();Serial.input="\n*IDN?\n";usb("");CHECK(Serial.output==""+identity);
}
void numeric_output_is_atomic_and_representable(){
    for(float value:{1e20f,-1e20f}){
        g_a2d_adc.voltage_scale=value;Serial.output.clear();usb("MEAS:VOLT? 0\n");CHECK(Serial.output=="");
        g_a2d_adc.cal_gain=value;Serial.output.clear();usb("CAL? 0\n");CHECK(Serial.output=="");
        g_a2d_adc.cal_gain=1;g_a2d_adc.cal_offset=value;Serial.output.clear();usb("CAL? 1\n");CHECK(Serial.output=="");
        g_a2d_adc.cal_offset=0;
    }
    g_a2d_adc.voltage_scale=4294967040.0f;Serial.output.clear();usb("MEAS:VOLT? 1\n");CHECK(Serial.output=="4294967040.0000\r\n");
    g_a2d_adc.voltage_scale=-4294967040.0f;Serial.output.clear();usb("MEAS:VOLT? 1\n");CHECK(Serial.output=="-4294967040.0000\r\n");
    g_a2d_adc.voltage_scale=1;Serial.output.clear();usb("MEAS:VOLT? 0\n*IDN?\n");CHECK(Serial.output=="1.0000,2.0000,3.0000,4.0000\r\n"+identity);
}
void command_ranges_reject_before_mutation_or_forwarding(){
    for(const char *channel:{"1","5","128"})for(const char *points:{
        "-0.001,0,5,2","0,0,5.001,2","0,0,1e20,1","0,0,0.0014,0.0005",
        "0,0,2,1","0,0,4,1","0,0,2.5,1","0,0,3.2,1",
        "0,1,2.5,0","0,0.2501,2.75,1.2501","0,0.23,5,2.0481"}){
        Serial.output.clear();Serial3.clear();
        usb(std::string("CAL:VOLT ")+channel+","+points+"\n");
        CHECK(Serial.output==""&&Serial3.output.empty()&&g_a2d_adc.calibration_calls==0);
    }
    for(const char *line:{"INSTR:LED 2","INSTR:LED -1","INSTR:GAIN 0,0","INSTR:GAIN 3,0","INSTR:GAIN 9,0",
        "INSTR:DR 0,0","INSTR:DR 14,0","INSTR:DR 241,0","INSTR:MODE 2,0","INSTR:RS485:SET 32",
        "MEAS:VOLT? 129","CAL:RESET 129","CAL:SAVE -1","CAL:VOLT 0,0,0,2.75,1"}){
        Serial.output.clear();Serial3.clear();usb(std::string(line)+"\n");
        CHECK(Serial.output==""&&Serial3.output.empty());
    }
    CHECK(g_a2d_adc.address==0&&!g_a2d_adc.led&&!g_a2d_adc.reset_mask&&!g_a2d_adc.save_mask&&!g_a2d_adc.calibration_calls);
    for(uint8_t value:g_a2d_adc.gains)CHECK(value==0);
    for(uint8_t value:g_a2d_adc.rates)CHECK(value==0);
    for(uint8_t value:g_a2d_adc.modes)CHECK(value==0);
    A2D_Request request;
    for(const char *line:{"INSTR:LED 0","INSTR:LED 1","INSTR:GAIN 1,0","INSTR:GAIN 8,128",
        "INSTR:DR 15,0","INSTR:DR 240,128","INSTR:MODE 0,0","INSTR:MODE 1,128",
        "INSTR:RS485:SET 0","INSTR:RS485:SET 31","MEAS:VOLT? 0","MEAS:VOLT? 128","CAL:SAVE 0"})
        CHECK(a2d_parse(line,A2D_SOURCE_USB,0,&request)==A2D_PARSE_READY);
}
void usb_bootloader_requires_a_complete_usb_command(){
    for(const char *line:{"SYST:BOOT?","SYST:BOOT 1","SYST:BOOT EXTRA","SYST:BOOT;*IDN?","1EAF","1EAF_NOT_A_RESET"}){
        Serial.output.clear();usb(std::string(line)+"\n");CHECK(!boot_requests&&Serial.output=="");
    }
    usb("SYST:BO");CHECK(!boot_requests);usb("OT\n");CHECK(boot_requests==1);
    rs485("0 123 SYST:BOOT\n");CHECK(boot_requests==1&&Serial3.output=="");
    A2D_Request request={};request.command=A2D_CMD_BOOTLOADER;request.source=A2D_SOURCE_RS485;
    a2d_dispatch_execute(&g_transport,&request);CHECK(boot_requests==1);
    usb(std::string(200,'X')+"SYST:BOOT\n");CHECK(boot_requests==1);
}
void usb_session_discards_partial_and_overflow_frames(){
    g_a2d_adc.address=7;usb("INSTR:RS485:SET 3");
    test_usb_session+=2;usb("1\nINSTR:RS485?\n");
    CHECK(g_a2d_adc.address==7&&Serial.output=="7\r\n");
    Serial.output.clear();usb(std::string(200,'X'));test_usb_session+=2;usb("*IDN?\n");CHECK(Serial.output==identity);
    Serial.output.clear();usb("SYST:BO");test_usb_session+=2;usb("OT\n");CHECK(!boot_requests&&Serial.output=="");
}
void usb_session_cancels_forward_without_reusing_ids(){
    Serial.input="MEAS:VOLT? 5\n";loop();CHECK(g_transport.waiting);
    const std::string previous=lastHeader();test_usb_session+=2;
    usb("*IDN?\n");CHECK(!g_transport.waiting&&Serial.output==identity&&test_clock<1000);
    Serial.output.clear();Serial3.on_flush=[previous]{Serial3.input=previous+"old\n"+lastHeader()+"new\n";};
    usb("MEAS:VOLT? 5\n");CHECK(Serial.output=="new\r\n"&&lastHeader()!=previous);
}
void serial_provisioning_is_usb_only_and_write_once(){
    g_a2d_adc.serial_value=0xffffffffUL;strcpy(g_a2d_adc.serial_text,"UNASSIGNED");
    for(const char *text:{"3","+003","00003","abcd","0003?","0003 EXTRA"}){
        Serial.output.clear();usb(std::string("SYST:SER:SET ")+text+"\n");CHECK(Serial.output=="");
    }
    Serial.output.clear();usb("SYST:SER:SET 0003\n*IDN?\n");
    CHECK(Serial.output=="A2D Electronics,4CH Isolated ADC,0003,V5.0.1\r\n");
    Serial.output.clear();usb("SYST:SER:SET 0004\n");CHECK(Serial.output=="");
    rs485("0 123 SYST:SER:SET 0004\n");CHECK(g_a2d_adc.serial_value==3);
}
void configuration_queries_local_and_group(){
    usb("INSTR:GAIN 1,1\nINSTR:GAIN 2,2\nINSTR:GAIN 4,3\nINSTR:GAIN 8,4\n"
        "INSTR:DR 15,1\nINSTR:DR 60,2\nINSTR:DR 240,3\nINSTR:DR 15,4\n"
        "INSTR:MODE 0,1\nINSTR:MODE 1,2\nINSTR:MODE 0,3\nINSTR:MODE 1,4\n");
    const A2D_Board before=g_a2d_adc;
    usb("INSTR:GAIN? 0\nINSTR:DR? 0\nINSTR:MODE? 0\n");
    CHECK(Serial.output=="1,2,4,8\r\n15,60,240,15\r\n0,1,0,1\r\n");
    const unsigned gains[]={1,2,4,8}, rates[]={15,60,240,15}, modes[]={0,1,0,1};
    for(unsigned ch=1;ch<=4;++ch){
        Serial.output.clear();usb("INSTR:GAIN? "+std::to_string(ch)+"\nINSTR:DR? "+std::to_string(ch)+"\nINSTR:MODE? "+std::to_string(ch)+"\n");
        CHECK(Serial.output==std::to_string(gains[ch-1])+"\r\n"+std::to_string(rates[ch-1])+"\r\n"+std::to_string(modes[ch-1])+"\r\n");
    }
    CHECK(!memcmp(&before,&g_a2d_adc,sizeof(before))&&Serial3.output.empty());
}
void configuration_query_validation_and_atomic_errors(){
    const A2D_Board before=g_a2d_adc;
    for(const char *line:{"INSTR:GAIN?","INSTR:DR? -1","INSTR:MODE? 129","INSTR:GAIN? 1,2","INSTR:DR? 1?","INSTR:MODE? ?1",
                         "INSTR:GAIN 4,1?","INSTR:DR 60,1?","INSTR:MODE 1,1?","INSTR:GAIN 1?"}){
        Serial.output.clear();usb(std::string(line)+"\n");CHECK(Serial.output=="");
    }
    CHECK(!memcmp(&before,&g_a2d_adc,sizeof(before))&&Serial3.output.empty());
    g_a2d_adc.failing_channel=2;
    for(const char *command:{"INSTR:GAIN? 0","INSTR:DR? 0","INSTR:MODE? 0"}){
        Serial.output.clear();usb(std::string(command)+"\n");CHECK(Serial.output=="");
    }
    g_a2d_adc.io_ok=false;Serial.output.clear();usb("INSTR:GAIN? 1\n");CHECK(Serial.output=="");
}
void configuration_queries_forward_and_timeout(){
    for(const auto &item:{std::make_pair("INSTR:GAIN? 5","8"),std::make_pair("INSTR:DR? 128","60"),std::make_pair("INSTR:MODE? 41","1")}){
        Serial.clear();Serial3.clear();respondWith(std::string(item.second)+"\r\n");usb(std::string(item.first)+"\n");
        CHECK(Serial.output==std::string(item.second)+"\r\n"&&!g_transport.waiting);
        CHECK(Serial3.output.find(item.first)!=std::string::npos);
    }
    Serial.clear();Serial3.clear();test_clock=0;usb("INSTR:GAIN? 5\n");
    CHECK(Serial.output==""&&test_clock>=1000);
}
void configuration_queries_addressed_rs485(){
    g_a2d_adc.address=31;g_a2d_adc.gains[3]=3;g_a2d_adc.rates[3]=1;g_a2d_adc.modes[3]=1;
    const A2D_Board before=g_a2d_adc;
    rs485("31 11 INSTR:GAIN? 128\n31 12 INSTR:DR? 128\n31 13 INSTR:MODE? 128\n");
    CHECK(Serial3.output=="31 11 8\r\n31 12 60\r\n31 13 1\r\n"&&Serial.output.empty());
    Serial3.output.clear();rs485("31 14 INSTR:GAIN? 0\n31 15 INSTR:DR? 0\n31 16 INSTR:MODE? 0\n30 17 INSTR:GAIN? 128\n");
    CHECK(Serial3.output.empty());
    rs485("31 18 INSTR:MODE? 124\n");CHECK(Serial3.output=="");
    CHECK(!memcmp(&before,&g_a2d_adc,sizeof(before)));
}
void standard_query_and_separator_contract(){
    const A2D_Board before=g_a2d_adc;
    for(const char *line:{"MEAS:VOLT 1?","MEAS:VOLT 1","MEAS:VOLT:ADC 1?","MEAS:VOLT:ADC 1","CAL 1?","CAL 1",
                         "INSTR:GAIN 4 1","INSTR:GAIN ,4,1","INSTR:GAIN 4,,1","INSTR:GAIN 4,1,","INSTR:DR 60, ,1",
                         "INSTR:LED ,1,","CAL:SAVE ,0,","CAL:VOLT 1,0,,0,2.75,1","SYST:SER:SET ,0003,",
                         "SYST:BOOT ,","SYST:BOOT;","INSTR:LED 1;*IDN?","instr:gain? 1",":INSTR:GAIN? 1"}){
        Serial.output.clear();usb(std::string(line)+"\n");
        CHECK(Serial.output==""&&Serial3.output.empty()&&!boot_requests);
        CHECK(!memcmp(&before,&g_a2d_adc,sizeof(before)));
    }
    Serial.output.clear();usb("INSTR:GAIN\t4 \t, \t1 \r\nINSTR:GAIN? 1\nMEAS:VOLT? 1\nMEAS:VOLT:ADC? 1\nCAL? 1\n");
    CHECK(Serial.output=="4\r\n1.0000\r\n0.1000\r\n0.00000,1.00000\r\n");
    CHECK(g_a2d_adc.gains[0]==2);
    Serial3.output.clear();rs485("0 123 INSTR:GAIN 8 1\n");
    CHECK(Serial3.output==""&&g_a2d_adc.gains[0]==2);
}
std::string errorLine(int16_t code){return std::to_string(code)+",\""+a2d_error_message(code)+"\"\r\n";}
void expectQueued(const std::string &command,int16_t code){
    Serial.output.clear();usb(command+"\n");CHECK(Serial.output.empty());
    CHECK(g_transport.errors.count==1&&a2d_scpi_errors_peek(&g_transport.errors)==code);
    usb("SYST:ERR?\n");CHECK(Serial.output==errorLine(code)&&!g_transport.errors.count);
}
void silent_setters_and_classified_errors(){
    for(const auto &test:{std::make_pair("BOGUS",-113),{"*IDN",-113},{"INSTR:LED 1;*RST",-102},
        {"INSTR:GAIN 4 1",-102},{"INSTR:LED hello",-128},{"INSTR:LED 2",-222},
        {"MEAS:VOLT? -1",-222},{"INSTR:GAIN 3,1",-224},{"CAL:VOLT 1,0,0,4,1",-222},
        {"CAL:VOLT 1,0,nan,2.75,1",-128},{"SYST:ERR? 1",-102},{"syst:err?",-113}})
        expectQueued(test.first,(int16_t)test.second);
    Serial.output.clear();usb("SYST:SER:SET 0002\nINSTR:LED 1\nINSTR:GAIN 4,1\nCAL:SAVE 1\n*TRG\n");
    CHECK(Serial.output.empty()&&!g_transport.errors.count&&g_a2d_adc.led);
    usb("SYST:ERR?\n");CHECK(Serial.output=="0,\"No error\"\r\n");
}
void fifo_overflow_clear_and_error_lifetime(){
    usb("BOGUS\nINSTR:LED 2\n");usb("*RST\n");
    test_usb_session+=2;loop();a2d_transport_reset(&g_transport);
    usb("SYST:ERR?\nSYST:ERR?\nSYST:ERR?\n");
    CHECK(Serial.output==errorLine(-113)+errorLine(-222)+errorLine(0));
    Serial.output.clear();for(int i=0;i<30;++i)usb("INSTR:LED 2\n");
    CHECK(Serial.output.empty()&&g_transport.errors.count==8);
    for(int i=0;i<9;++i)usb("SYST:ERR?\n");
    std::string expected;for(int i=0;i<7;++i)expected+=errorLine(-222);
    CHECK(Serial.output==expected+errorLine(-350)+errorLine(0));
    usb("BOGUS\n");const A2D_Board before=g_a2d_adc;
    Serial.output.clear();usb("*CLS\nSYST:ERR?\n");
    CHECK(Serial.output==errorLine(0)&&!memcmp(&before,&g_a2d_adc,sizeof(before)));
}
void device_failures_are_only_queued(){
    g_a2d_adc.failing_channel=2;expectQueued("MEAS:VOLT? 0",1001);g_a2d_adc.failing_channel=-1;
    g_a2d_adc.io_ok=false;expectQueued("INSTR:GAIN 2,0",1002);expectQueued("INSTR:MODE? 0",1002);g_a2d_adc.io_ok=true;
    g_a2d_adc.storage_ok=false;expectQueued("CAL:SAVE 0",-320);expectQueued("*RST",1004);g_a2d_adc.storage_ok=true;
    expectQueued("SYST:SER:SET 0003",1003);
    expectQueued("MEAS:VOLT? 5",1101);
    Serial3.reject_write=true;expectQueued("INSTR:GAIN 4,5",1102);Serial3.reject_write=false;
    Serial3.endlessly_readable=true;Serial.output.clear();usb("MEAS:VOLT? 5\n");Serial3.endlessly_readable=false;
    CHECK(Serial.output.empty()&&a2d_scpi_errors_pop(&g_transport.errors)==1103);
    Serial.output.clear();CHECK(!a2d_transport_reply(&g_transport,A2D_SOURCE_USB,std::string(126,'X').c_str()));
    CHECK(Serial.output.empty()&&!g_transport.usb_pending_size&&a2d_scpi_errors_pop(&g_transport.errors)==-400);
}
void queued_error_reads_respect_backpressure(){
    usb("INSTR:LED 2\n");Serial.space=0;Serial.input="SYST:ERR?\nSYST:ERR?\n";loop();
    CHECK(pendingUsbReply()==errorLine(-222)&&!g_transport.errors.count&&Serial.output.empty());
    const auto position=g_transport.usb_line.position;for(int i=0;i<100;++i)loop();
    CHECK(g_transport.usb_line.position==position&&pendingUsbReply()==errorLine(-222));
    Serial.space=127;usb("");CHECK(Serial.output==errorLine(-222)+errorLine(0));
    Serial.output.clear();usb("BOGUS\n");Serial.space=0;Serial.input="*IDN?\n";loop();
    A2D_Request request={};request.command=A2D_CMD_ERROR;request.source=A2D_SOURCE_USB;
    a2d_dispatch_execute(&g_transport,&request);
    CHECK(g_transport.errors.count==1&&pendingUsbReply()==identity);
    Serial.space=127;usb("");CHECK(Serial.output==identity);
    Serial.output.clear();usb("SYST:ERR?\n");CHECK(Serial.output==errorLine(-113));
}
void rs485_errors_are_local_and_only_queried(){
    g_a2d_adc.address=1;rs485("2 1 BOGUS\n");CHECK(!g_transport.errors.count);
    rs485("1 2 BOGUS\n1 3 INSTR:LED 2\n");CHECK(Serial.output.empty()&&Serial3.output.empty()&&g_transport.errors.count==2);
    rs485("1 4 SYST:ERR?\n1 5 SYST:ERR?\n1 6 SYST:ERR?\n");
    CHECK(Serial3.output=="1 4 "+errorLine(-113)+"1 5 "+errorLine(-222)+"1 6 "+errorLine(0));
    Serial3.output.clear();rs485("1 7 SYST:BOOT\n");CHECK(Serial3.output.empty()&&!boot_requests);
    rs485("1 8 SYST:ERR?\n");CHECK(Serial3.output=="1 8 "+errorLine(-200));
    Serial3.output.clear();rs485("1 9 BOGUS\n1 10 *CLS\n");CHECK(Serial3.output.empty()&&!g_transport.errors.count);
}
int main(){
    const struct{const char *name;void(*run)();} tests[]={
#define TEST(n) {#n,n}
        TEST(silent_setters_and_classified_errors),TEST(fifo_overflow_clear_and_error_lifetime),TEST(device_failures_are_only_queued),
        TEST(queued_error_reads_respect_backpressure),TEST(rs485_errors_are_local_and_only_queried),TEST(standard_query_and_separator_contract),TEST(configuration_queries_local_and_group),TEST(configuration_query_validation_and_atomic_errors),
        TEST(configuration_queries_forward_and_timeout),TEST(configuration_queries_addressed_rs485),
        TEST(identity_and_measurement),TEST(partial_usb_services_rs485),TEST(line_boundaries_and_recovery),TEST(invalid_arguments_do_not_mutate),
        TEST(all_channel_configuration),TEST(forwarded_queries_and_writes),TEST(missing_or_unterminated_peer),TEST(oversized_untrusted_peer_is_discarded),
        TEST(timeout_wrap),TEST(addressed_peer_and_foreign_silence),TEST(last_channel_and_identity),TEST(malformed_envelopes_stay_silent),
        TEST(forwarding_boundaries),TEST(calibration_overflow_before_routing),TEST(nonblocking_and_ordered_forwarding),TEST(stale_wrong_address_and_wrong_id_rejected),
        TEST(stale_reply_from_previous_boot_is_rejected),TEST(remaining_commands_and_point_order),TEST(owned_request_and_rs485_groups),TEST(setters_with_question_mark_are_rejected),TEST(failure_responses_are_atomic),
        TEST(usb_backpressure_disconnect_and_recovery),TEST(continuous_input_yields),TEST(numeric_output_is_atomic_and_representable),TEST(command_ranges_reject_before_mutation_or_forwarding),
        TEST(usb_bootloader_requires_a_complete_usb_command),TEST(usb_session_discards_partial_and_overflow_frames),TEST(usb_session_cancels_forward_without_reusing_ids),TEST(serial_provisioning_is_usb_only_and_write_once),
        TEST(usb_replies_are_ordered_under_backpressure),TEST(forwarded_replies_and_timeouts_are_ordered_under_backpressure),
        TEST(continuous_complete_usb_services_rs485),TEST(pending_usb_reply_services_rs485),
        TEST(pending_usb_reply_is_purged_by_session_disconnect_and_reset),TEST(usb_pending_boundaries_and_failed_enqueue),
        TEST(forward_drain_is_bounded_and_reports_busy),TEST(failed_forward_writes_stop_without_waiting_or_retry),
        TEST(failed_forward_write_respects_usb_backpressure),TEST(rs485_replies_are_single_checked_frames),TEST(usb_reply_status_preserves_pending_output)};
    for(const auto &t:tests){reset_test();try{t.run();}catch(const std::exception &e){std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<'\n';return 1;}std::cout<<"PASS "<<t.name<<'\n';}
    std::cout<<sizeof(tests)/sizeof(tests[0])<<" application groups passed\n";
}
