#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_protocol.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace opentrail::companion;
namespace {
int failures = 0;
#define EXPECT(x) do { if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (false)
std::vector<std::string> split(const std::string& line, char separator) {
    std::istringstream input(line); std::vector<std::string> fields; std::string value;
    while (std::getline(input,value,separator)) fields.push_back(value);
    return fields;
}
std::vector<std::uint8_t> unhex(const std::string& text) {
    if (text == "-") return {};
    EXPECT(text.size()%2 == 0);
    std::vector<std::uint8_t> bytes;
    for (std::size_t i=0; i+1<text.size(); i+=2) bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
    return bytes;
}
void corpus(const char* path) {
    std::ifstream input(path); EXPECT(input.is_open());
    std::string line; std::set<std::string> names; std::size_t yes=0,no=0;
    while (std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty() || line.front()=='#') continue;
        const auto c=split(line,'\t'); EXPECT(c.size()==5); if(c.size()!=5) continue;
        EXPECT(names.insert(c[0]).second); EXPECT(c[2]=="1" || c[2]=="0");
        const bool expected=c[2]=="1"; const auto bytes=unhex(c[3]); const auto fields=split(c[4],'|');
        std::array<std::uint8_t,150> output{}; output.fill(0xa5);
        ConfigurationEncodeResult encoded{}; bool accepted=false;
        if(c[1]=="info") {
            const auto d=decode_configuration_info(bytes.data(),bytes.size()); accepted=d.decoded();
            if (!accepted) EXPECT(!configuration_transport_compatible(d.value,0x40,151,148,148,true));
            if(accepted && expected) {
                EXPECT(fields.size()==1); EXPECT(d.value.capabilities==std::stoul(fields[0]));
                encoded=encode_configuration_info(d.value,output.data()+1,bytes.size());
                EXPECT(!decode_companion_protocol_info({bytes.data(),bytes.size()}).decoded());
            }
        } else if(c[1]=="time") {
            const auto d=decode_configuration_time_payload(bytes.data(),bytes.size()); accepted=d.decoded();
            if(accepted && expected) {
                EXPECT(fields.size()==5);
                if(fields.size()==5) {
                    EXPECT(d.value.kind==std::stoul(fields[0])); EXPECT(d.value.code==std::stoul(fields[1]));
                    EXPECT(d.value.format==std::stoul(fields[2])); EXPECT(d.value.challenge_id==std::stoull(fields[3]));
                    EXPECT(d.value.local_second_of_day==std::stoul(fields[4]));
                }
                encoded=encode_configuration_time_payload(d.value,output.data()+1,bytes.size());
            }
        } else if(c[1]=="frame") {
            const auto d=decode_configuration_frame(bytes.data(),bytes.size()); accepted=d.decoded();
            if(accepted && expected) {
                EXPECT(fields.size()==4);
                if(fields.size()==4) {
                    EXPECT(d.value.kind==std::stoul(fields[0])); EXPECT(d.value.session_nonce==std::stoul(fields[1]));
                    EXPECT(d.value.exchange_id==std::stoul(fields[2])); const auto payload=unhex(fields[3]);
                    EXPECT(d.value.payload_bytes==payload.size()); EXPECT(std::equal(payload.begin(),payload.end(),d.value.payload.begin()));
                }
                encoded=encode_configuration_frame(d.value,output.data()+1,bytes.size());
                EXPECT(!decode_companion_fragment({bytes.data(),bytes.size()}).decoded());
            }
        } else EXPECT(false);
        if(accepted!=expected) std::cerr << "CORPUS " << c[0] << '\n';
        EXPECT(accepted==expected);
        if(expected) {
            ++yes; EXPECT(encoded.encoded() && encoded.encoded_bytes==bytes.size());
            EXPECT(std::equal(bytes.begin(),bytes.end(),output.begin()+1));
            EXPECT(output.front()==0xa5);
            EXPECT(std::all_of(output.begin()+1+bytes.size(),output.end(),[](auto b){return b==0xa5;}));
        } else { ++no; EXPECT(c[4]=="-"); }
    }
    EXPECT(yes>0 && no>0);
    std::cout << "Shared corpus: " << yes << " accepted, " << no << " rejected\n";
}
void helper_and_nonmutation() {
    for(unsigned c=0;c<256;++c) {
        ConfigurationInfo info{static_cast<std::uint8_t>(c)};
        const bool valid=(c&0x10)==0 && (c&0xc0)!=0;
        for(unsigned request=0;request<256;++request) {
            EXPECT(configuration_transport_compatible(info,static_cast<std::uint8_t>(request),151,148,148,true)==
                (valid && (request==0x40 || request==0x80) && (c&request)!=0));
        }
    }
    const ConfigurationInfo info{0xef};
    EXPECT(!configuration_transport_compatible(info,0x40,150,148,148,true));
    EXPECT(!configuration_transport_compatible(info,0x40,151,147,148,true));
    EXPECT(!configuration_transport_compatible(info,0x40,151,148,147,true));
    EXPECT(!configuration_transport_compatible(info,0x40,151,148,148,false));
    EXPECT(configuration_transport_compatible(info,0x80,517,512,512,true));
    std::array<std::uint8_t,150> output{}; output.fill(0xa5); const auto original=output;
    EXPECT(!encode_configuration_info({0x10},output.data(),output.size()).encoded()); EXPECT(output==original);
    EXPECT(!encode_configuration_info(info,output.data(),15).encoded()); EXPECT(output==original);
    EXPECT(!encode_configuration_info(info,nullptr,16).encoded());
    ConfigurationTimePayload time{};
    EXPECT(!encode_configuration_time_payload(time,output.data(),23).encoded()); EXPECT(output==original);
    time.kind=3; time.format=2; time.challenge_id=1; time.local_second_of_day=86400;
    EXPECT(!encode_configuration_time_payload(time,output.data(),output.size()).encoded()); EXPECT(output==original);
    EXPECT(!encode_configuration_time_payload({},nullptr,24).encoded());
    ConfigurationFrame frame{}; frame.session_nonce=1; frame.exchange_id=1; frame.payload_bytes=128;
    EXPECT(!encode_configuration_frame(frame,output.data(),147).encoded()); EXPECT(output==original);
    frame.payload_bytes=129;
    EXPECT(!encode_configuration_frame(frame,output.data(),output.size()).encoded()); EXPECT(output==original);
    frame.payload_bytes=0; frame.kind=3;
    EXPECT(!encode_configuration_frame(frame,output.data(),output.size()).encoded()); EXPECT(output==original);
    frame.kind=1; EXPECT(!encode_configuration_frame(frame,nullptr,148).encoded());
    EXPECT(!decode_configuration_info(nullptr,16).decoded());
    EXPECT(!decode_configuration_time_payload(nullptr,24).decoded());
    EXPECT(!decode_configuration_frame(nullptr,20).decoded());
}
}
int main(int argc,char** argv) {
    helper_and_nonmutation();
    corpus(argc>1?argv[1]:"tests/fixtures/companion_configuration_v02.tsv");
    if(failures) return EXIT_FAILURE;
    std::cout << "PASS: 2 configuration codec groups (candidate only)\n";
    return EXIT_SUCCESS;
}
