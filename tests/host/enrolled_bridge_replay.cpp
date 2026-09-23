// OT0247e host-only process adapter; unchanged actual composed stack and driver.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main composed_fixture_main
#include "enrolled_completion_composed_tests.cpp"
#undef main
#pragma GCC diagnostic pop
#include <iostream>

struct ReplayObserver final : EnrolledSessionObserver {
 FaultObserver value;std::uint64_t issued=0,deadline=0;
 void rejection(const EnrolledFailureDetail& v)override{value.rejection(v);}
 void milestone(EnrolledMilestone e,std::uint64_t n,std::uint64_t i,std::uint64_t d)override{issued=i;deadline=d;value.milestone(e,n,i,d);}
 void tick_begin(bool v)override{value.tick_begin(v);}void tick_end()override{value.tick_end();}
 void fault(EnrolledFault v)override{value.fault(v);}void before_cleanup(bool v)override{value.before_cleanup(v);}
};
struct Chip {
 std::uint16_t irq=0;std::size_t length=0,bytes=0;
 std::array<std::uint8_t,255> packet{},sent{};
 std::array<int,47> gpio{};
 std::int64_t due=-1;bool delivered=false;
};
int main(int argc,char** argv){
 CHECK(argc==3);
 radio_mock::reset();nvs_mock::reset();read_cost_us=0;sdk_reads=0;
 ReplayObserver fa,fb;Peer a(1,&fa),b(2,&fb);
 a.driver.physical_mode=b.driver.physical_mode=true;
 // Rebase both synthetic local clock origins for the real driver; IDs remain distinct.
 b.source.value.now_ms=100;
 read_cost_us=static_cast<unsigned>(std::stoul(argv[1]));
 const auto airtime=static_cast<std::int64_t>(std::stoll(argv[2]));
 Chip chips[2];Peer* peers[]={&a,&b};int current=0;bool pressed[2]={false,false};
 bool fail_rearm=false;std::int64_t max_radio_lateness=0;
 auto load=[&](int i){current=i;auto&c=chips[i];radio_mock::irq=c.irq;radio_mock::length=c.length;radio_mock::packet=c.packet;radio_mock::transmitted=c.sent;radio_mock::transmitted_bytes=c.bytes;radio_mock::gpio=c.gpio;};
 auto save=[&]{auto&c=chips[current];c.irq=radio_mock::irq;c.length=radio_mock::length;c.packet=radio_mock::packet;c.sent=radio_mock::transmitted;c.bytes=radio_mock::transmitted_bytes;c.gpio=radio_mock::gpio;};
 radio_mock::callback=[&](const std::string& op){
  auto& c=chips[current];
  if(op=="start_tx"){c.due=radio_mock::now_us;c.delivered=false;}
  if((op=="dio"||op=="irq")&&c.due>=0&&radio_mock::now_us>=c.due+(airtime ? static_cast<std::int64_t>((20.25+5*((8*radio_mock::transmitted_bytes+16+27)/28))*1024) : 0)){
   const auto due=c.due+(airtime ? static_cast<std::int64_t>((20.25+5*((8*radio_mock::transmitted_bytes+16+27)/28))*1024) : 0);
   max_radio_lateness=std::max(max_radio_lateness,radio_mock::now_us-due);
   radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;
   if(!c.delivered){auto&dst=chips[1-current];dst.packet=radio_mock::transmitted;dst.length=radio_mock::transmitted_bytes;
    if(peers[1-current]->driver.receive_ready()){dst.irq=RADIOLIB_SX126X_IRQ_RX_DONE;}
    c.delivered=true;}
  }
  if(op=="finish_tx")c.due=-1;
  if(op=="start_rx"&&fail_rearm){radio_mock::fault="start_rx";radio_mock::matching=0;}
 };
 std::cout<<"READY\n"<<std::flush;
 std::string line;
 while(std::getline(std::cin,line)){
  std::istringstream in(line);std::string op;in>>op;
  if(op=="QUIT")break;
  std::string response;bool ok=true;unsigned before=sdk_reads;
  if(op=="ADV"){std::int64_t target;in>>target;CHECK(target>=radio_mock::now_us);radio_mock::now_us=target;}
  else if(op=="REARMFAIL")fail_rearm=true;
  else {
   int role;in>>role;CHECK(role==0||role==1);load(role);auto&e=*peers[role];
   if(op=="BUTTON"){int level;in>>level;pressed[role]=level!=0;ok=e.session.tick(pressed[role]);}
   else if(op=="LOSS"){++e.source.value.context.session_nonce;}
   else if(op=="CMD"){
    std::string command;std::getline(in,command);if(!command.empty()&&command[0]==' ')command.erase(0,1);
    (void)e.session.tick(pressed[role]);EnrolledBenchSession::Output out{};std::size_t count=0;
    ok=e.session.command(command,out,count);response.assign(out.data(),count);
    if(response.empty())response="OTENROLL1 REFUSED 0\n";
   }else CHECK(false);
   save();
  }
  std::cout<<"RESULT "<<radio_mock::now_us<<" "<<sdk_reads-before<<" "<<ok<<" "
   <<static_cast<unsigned>(fa.value.first)<<" "<<static_cast<unsigned>(fb.value.first)<<" "
   <<static_cast<unsigned>(fa.value.detail.reason)<<" "<<static_cast<unsigned>(fb.value.detail.reason)<<" "
   <<a.session.secrets_cleared()<<" "<<b.session.secrets_cleared()<<" "
   <<fa.issued<<" "<<fa.deadline<<" "<<fb.issued<<" "<<fb.deadline<<" "<<max_radio_lateness<<" "
   <<static_cast<unsigned>(fa.value.detail.layer)<<" "<<fa.value.detail.now_ms<<" "<<fa.value.detail.deadline_ms<<" "
   <<static_cast<unsigned>(fb.value.detail.layer)<<" "<<fb.value.detail.now_ms<<" "<<fb.value.detail.deadline_ms<<" "
   <<hex(reinterpret_cast<const unsigned char*>(response.data()),response.size())<<"\n"<<std::flush;
 }
}
