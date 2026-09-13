#include "opentrail/companion_confirmation_codec.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
using namespace opentrail::companion;
namespace {
int groups=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<std::uint8_t> unhex(const std::string& s){
 std::vector<std::uint8_t> b;for(std::size_t i=0;i<s.size();i+=2)b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i,2),nullptr,16)));return b;
}
}
int main(int argc,char** argv){try{
 require(argc==2,"fixture argument");std::ifstream input(argv[1]);require(bool(input),"fixture open");
 const std::string json((std::istreambuf_iterator<char>(input)),{});
 const std::regex pattern("\"payload_hex\": \"([0-9a-f]+)\"");
 std::vector<std::uint8_t> offer;
 int fixtures=0;
 for(auto i=std::sregex_iterator(json.begin(),json.end(),pattern);i!=std::sregex_iterator();++i){
  const auto bytes=unhex((*i)[1]);const auto d=decode_confirmation_payload(bytes.data(),bytes.size());require(d.decoded(),"golden decode");
  std::array<std::uint8_t,128> encoded{};require(encode_confirmation_payload(d.value,encoded.data(),encoded.size()).encoded(),"golden encode");
  require(std::equal(encoded.begin(),encoded.end(),bytes.begin()),"independent golden bytes");
  if(d.value.role){require(d.value.attempt==0x0102030405060708ULL && d.value.group==0xfedcba9876543210ULL && d.value.epoch==0x12345678,"numeric golden fields");
   require(d.value.transport_generation==0x8877665544332211ULL && d.value.session_nonce==0xaabbccdd,"context golden fields");}
  if(d.value.kind==4 && d.value.role==1 && d.value.remaining_ms==15000)offer=bytes;
  ++fixtures;++groups;
 }
 require(fixtures==11 && offer.size()==128,"fixture count");
 for(std::size_t size=0;size<128;++size)require(!decode_confirmation_payload(offer.data(),size).decoded(),"short frame accepted");
 auto longer=offer;longer.push_back(0);require(!decode_confirmation_payload(longer.data(),longer.size()).decoded(),"trailing byte accepted");++groups;
 require(!decode_confirmation_payload(nullptr,128).decoded(),"null input accepted");++groups;
 for(auto offset:{0,1,2,3,4,5,6,7,124,125,126,127}){
  auto bad=offer;bad[offset]=0xff;require(!decode_confirmation_payload(bad.data(),bad.size()).decoded(),"bad tag or reserved accepted");
 }++groups;
 for(const auto range:std::vector<std::pair<int,int>>{{8,8},{16,8},{24,4},{28,16},{44,32},{76,32},{108,4},{112,8},{120,4}}){
  auto bad=offer;std::fill(bad.begin()+range.first,bad.begin()+range.first+range.second,0);require(!decode_confirmation_payload(bad.data(),bad.size()).decoded(),"zero binding accepted");
 }++groups;
 auto bad=offer;bad[15]=0x80;require(!decode_confirmation_payload(bad.data(),bad.size()).decoded(),"signed attempt overflow");
 bad=offer;bad[108]=0x61;bad[109]=0xea;bad[110]=0;bad[111]=0;require(!decode_confirmation_payload(bad.data(),bad.size()).decoded(),"TTL over maximum");++groups;
 auto decoded=decode_confirmation_payload(offer.data(),offer.size()).value;
 std::array<std::uint8_t,128> destination{};destination.fill(0xa5);const auto untouched=destination;
 require(!encode_confirmation_payload(decoded,destination.data(),127).encoded() && destination==untouched,"short output mutated");
 decoded.kind=255;require(!encode_confirmation_payload(decoded,destination.data(),128).encoded() && destination==untouched,"invalid output mutated");++groups;
 const auto original=decode_confirmation_payload(offer.data(),offer.size()).value;auto decision=original;decision.kind=2;
 require(same_confirmation_offer(original,decision),"decision equality");decision.status=3;require(same_confirmation_offer(original,decision),"result comparison excludes status");
 decision.remaining_ms-=1;require(!same_confirmation_offer(original,decision),"TTL echo mismatch");decision=original;decision.nonce[3]^=1;require(!same_confirmation_offer(original,decision),"nonce mismatch");++groups;
 ConfirmationPayload empty{};empty.kind=5;empty.status=4;require(encode_confirmation_payload(empty,destination.data(),128).encoded(),"unavailable encode");
 empty.role=1;require(!encode_confirmation_payload(empty,destination.data(),128).encoded(),"unavailable leaked descriptor");
 empty={};empty.status=1;require(!encode_confirmation_payload(empty,destination.data(),128).encoded(),"read status accepted");++groups;
 std::cout<<"PASS "<<groups<<" confirmation codec groups; 11 independent wire vectors\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
