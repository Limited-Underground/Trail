// Actual persistent OT216 backend, real libsodium/Noise, SDK boundaries only.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "confirmation_evaluation_backend.hpp"
#include "esp_bt.h"
#include "opentrail/evaluation_replay_store.hpp"
using namespace opentrail;
using namespace opentrail::companion;
using namespace opentrail::target::heltec_v4_bench;
#define CHECK(x) do { if (!(x)) { std::cerr << "failed " << __LINE__ << " " #x "\n"; std::exit(1); } } while(0)
namespace mock {
using Bytes=std::vector<unsigned char>;
using Blobs=std::map<std::string,Bytes>;
struct Handle { std::string name; Blobs pending; };
std::map<std::string,Blobs> durable;
std::map<nvs_handle_t,Handle> handles;
std::map<std::string,unsigned> writes;
unsigned next_handle=1, record_crypto=0, splits=0, random_calls=0, match=0, fail_nth=1;
std::string fail_operation, fail_name;
bool sodium_fails=false;
esp_bt_controller_status_t controller=ESP_BT_CONTROLLER_STATUS_ENABLED;
std::function<void()> random_callback=[]{}, storage_callback=[]{};
std::uint64_t rng=0x123456789abcdef0ULL;
bool fail(const char* op,const std::string& name) {
    return fail_operation==op && (fail_name.empty() || name==fail_name) && ++match==fail_nth;
}
void reset() { CHECK(handles.empty()); durable.clear(); writes.clear();next_handle=1;record_crypto=splits=random_calls=match=0;
    fail_nth=1;fail_operation.clear();fail_name.clear();sodium_fails=false;controller=ESP_BT_CONTROLLER_STATUS_ENABLED;
    random_callback=[]{};storage_callback=[]{};rng=0x123456789abcdef0ULL; }
unsigned mutations() { unsigned result=0;for(const auto& row:writes)result+=row.second;return result; }
void fault(const std::string& op,const std::string& name,unsigned nth=1) { fail_operation=op;fail_name=name;match=0;fail_nth=nth; }
void apply(Handle& h) { for(const auto& [key,bytes]:h.pending) {if(bytes.empty())durable[h.name].erase(key);else durable[h.name][key]=bytes;} h.pending.clear(); }
}
esp_bt_controller_status_t esp_bt_controller_get_status() { return mock::controller; }
void esp_fill_random(void* output,std::size_t size) {
    ++mock::random_calls;mock::random_callback();auto* bytes=static_cast<unsigned char*>(output);
    for(std::size_t i=0;i<size;++i){mock::rng^=mock::rng<<13;mock::rng^=mock::rng>>7;mock::rng^=mock::rng<<17;bytes[i]=static_cast<unsigned char>(mock::rng);}
}
extern "C" int __wrap_sodium_init() { return mock::sodium_fails ? -1 : 0; }
extern "C" int __real_ot_noise_xk_split(ot_noise_xk_state*,unsigned char*,unsigned char*);
extern "C" int __wrap_ot_noise_xk_split(ot_noise_xk_state* state,unsigned char* a,unsigned char* b) {++mock::splits;return __real_ot_noise_xk_split(state,a,b);}
extern "C" int __real_crypto_aead_chacha20poly1305_ietf_encrypt(unsigned char*,unsigned long long*,const unsigned char*,unsigned long long,const unsigned char*,unsigned long long,const unsigned char*,const unsigned char*,const unsigned char*);
extern "C" int __wrap_crypto_aead_chacha20poly1305_ietf_encrypt(unsigned char* c,unsigned long long* clen,const unsigned char* m,unsigned long long mlen,const unsigned char* ad,unsigned long long adlen,const unsigned char* nsec,const unsigned char* nonce,const unsigned char* key){
    if(adlen==148) { ++mock::record_crypto; }
    return __real_crypto_aead_chacha20poly1305_ietf_encrypt(c,clen,m,mlen,ad,adlen,nsec,nonce,key);
}
extern "C" int __real_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char*,unsigned long long*,unsigned char*,const unsigned char*,unsigned long long,const unsigned char*,unsigned long long,const unsigned char*,const unsigned char*);
extern "C" int __wrap_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char* m,unsigned long long* mlen,unsigned char* nsec,const unsigned char* c,unsigned long long clen,const unsigned char* ad,unsigned long long adlen,const unsigned char* nonce,const unsigned char* key){
    if(adlen==148) { ++mock::record_crypto; }
    return __real_crypto_aead_chacha20poly1305_ietf_decrypt(m,mlen,nsec,c,clen,ad,adlen,nonce,key);
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out) {
    mock::storage_callback();if(mock::fail("open",name))return ESP_FAIL;
    if(mode==NVS_READONLY&&!mock::durable.count(name))return ESP_ERR_NVS_NOT_FOUND;
    *out=mock::next_handle++;mock::handles[*out]={name,{}};if(mode==NVS_READWRITE)mock::durable[name];return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { CHECK(mock::handles.erase(handle)==1); }
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* size) {
    auto& handle=mock::handles.at(h);mock::storage_callback();
    if(mock::fail(out?"data_error":"query_error",handle.name))return ESP_FAIL;
    if(mock::fail(out?"data_short":"query_short",handle.name)){*size=63;return ESP_OK;}
    auto& blobs=mock::durable[handle.name];const auto i=blobs.find(key);if(i==blobs.end())return ESP_ERR_NVS_NOT_FOUND;
    if(!out){*size=i->second.size();return ESP_OK;}CHECK(*size>=i->second.size());
    std::memcpy(out,i->second.data(),i->second.size());*size=i->second.size();
    if(mock::fail("data_corrupt",handle.name))static_cast<unsigned char*>(out)[0]^=1;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t size) {
    auto& handle=mock::handles.at(h);mock::storage_callback();++mock::writes[handle.name];CHECK(size==64);
    if(mock::fail("set",handle.name))return ESP_FAIL;
    const auto* b=static_cast<const unsigned char*>(data);handle.pending[key]={b,b+size};return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key) {
    auto& handle=mock::handles.at(h);mock::storage_callback();++mock::writes[handle.name];
    if(mock::fail("erase",handle.name))return ESP_FAIL;
    if(!mock::durable[handle.name].count(key))return ESP_ERR_NVS_NOT_FOUND;
    handle.pending[key]={};return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    auto& handle=mock::handles.at(h);mock::storage_callback();++mock::writes[handle.name];
    if(mock::fail("commit",handle.name))return ESP_FAIL;
    mock::apply(handle);
    return mock::fail("commit_applied",handle.name)?ESP_FAIL:ESP_OK;
}
struct Source final:DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::ready,{1,2,3,4,5,6,7},100};
    std::function<void()> callback=[]{};
    DeviceNameAuthority current() noexcept override {callback();return value;}
};
static ConfigurationFrame request(const ConfirmationPayload& value) {
    ConfigurationFrame frame{};frame.kind=7;frame.minor_version=127;frame.session_nonce=7;frame.exchange_id=1;
    const auto encoded=encode_confirmation_payload(value,frame.payload.data(),frame.payload.size());CHECK(encoded.encoded());
    frame.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);return frame;
}
static ConfirmationPayload payload(const ConfigurationFrame& frame) {
    CHECK(frame.kind==0x89 && frame.minor_version==127 && frame.payload_bytes==128);
    const auto decoded=decode_confirmation_payload(frame.payload.data(),frame.payload_bytes);CHECK(decoded.decoded());return decoded.value;
}
static ConfirmationPayload get_offer(ConfirmationEvaluationBackend& backend,Source& source) {
    ConfigurationFrame out{};CHECK(backend.execute(source.value.context,request({}),out));auto offer=payload(out);
    CHECK(offer.kind==4 && offer.role==1 && offer.attempt==1 && offer.group==1 && offer.epoch==1);
    CHECK(offer.remaining_ms>0 && offer.remaining_ms<=60000 && offer.transport_generation==5 && offer.session_nonce==7);
    CHECK(mock::splits==0 && mock::record_crypto==0);return offer;
}
static void no_b_traffic() {CHECK(mock::splits<=1 && mock::record_crypto==0 && mock::writes["ot216_tb"]==0 && mock::writes["ot216_rb"]==0);}
static void a_retired() {
    const auto& bytes=mock::durable.at("ot216_ra").begin()->second;CHECK(bytes.size()==64);
    security_eval::EvaluationReplayStore::Context context{};std::copy_n(bytes.begin()+12,context.size(),context.begin());
    {ConfirmationNvsBackend backend(ConfirmationNvsBackend::Store::rx_a);persistence::PersistentStorageKv storage(backend);
     security_eval::EvaluationReplayStore replay(storage);CHECK(replay.start(context,false)==security_eval::ReplayError::retired);}
}
int main() {
    unsigned groups=0;
    for(bool confirm:{false,true}) {
        mock::reset();Source source;{ConfirmationEvaluationBackend backend(source);CHECK(mock::handles.empty()&&mock::mutations()==0&&mock::random_calls==0);
            auto offer=get_offer(backend,source);CHECK(mock::durable.size()==7);offer.kind=confirm?2:3;ConfigurationFrame out{};
            CHECK(backend.execute(source.value.context,request(offer),out));const auto result=payload(out);
            CHECK(result.kind==5 && result.status==(confirm?1:2) && same_confirmation_offer(result,offer));
            CHECK(mock::handles.empty());no_b_traffic();CHECK(mock::splits==(confirm?1U:0U));if(confirm)a_retired();
            const auto count=mock::mutations();CHECK(!backend.execute(source.value.context,request(offer),out));CHECK(mock::mutations()==count);
            CHECK(backend.close());}
        CHECK(mock::handles.empty());{ConfirmationEvaluationBackend reopened(source);ConfigurationFrame out{};CHECK(reopened.execute(source.value.context,request({}),out));CHECK(payload(out).status==4);}
        ++groups;
    }
    for(unsigned invalid=0;invalid<9;++invalid) {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);offer.kind=2;
        if(invalid==0)source.value.now_ms=60100;
        if(invalid==1)source.value.now_ms=99;
        if(invalid==2)source.value.context.owner_generation++;
        if(invalid==3)source.value.context.controller++;
        if(invalid==4)source.value.context.transport_generation++;
        if(invalid==5)source.value.phase=DeviceNamePhase::connected;
        if(invalid==6)source.value.phase=DeviceNamePhase::revoked;
        if(invalid==7)offer.remaining_ms--;
        if(invalid==8)offer.transcript[0]^=1;
        ConfigurationFrame out{};const bool handled=backend.execute({1,2,3,4,5,6,7},request(offer),out);
        CHECK(!handled || payload(out).status==3);CHECK(mock::handles.empty());CHECK(mock::splits==0);no_b_traffic();++groups;
    }
    for(auto phase:{DeviceNamePhase::unavailable,DeviceNamePhase::connected,DeviceNamePhase::revoked}) {
        mock::reset();Source source;source.value.phase=phase;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};
        CHECK(!backend.execute(source.value.context,request({}),out));CHECK(mock::handles.empty()&&mock::random_calls==0&&mock::mutations()==0);++groups;
    }
    for(auto controller:{ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED}) {
        mock::reset();mock::controller=controller;Source source;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};
        CHECK(backend.execute(source.value.context,request({}),out));CHECK(payload(out).status==4 && mock::random_calls==0 && mock::durable.empty());++groups;
    }
    for(const auto* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"}) {
        mock::reset();mock::durable[name];Source source;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};
        CHECK(backend.execute(source.value.context,request({}),out));CHECK(payload(out).status==4&&mock::mutations()==0&&mock::handles.empty());++groups;
    }
    for(const auto* name:{"ot216_boot","ot216_ia","ot216_ib"}) for(const auto* failure:{"open","set","commit","commit_applied","query_error","query_short","data_error","data_short","data_corrupt"}) {
        mock::reset();mock::fault(failure,name);Source source;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};
        CHECK(backend.execute(source.value.context,request({}),out));CHECK(payload(out).status==4&&mock::handles.empty());no_b_traffic();++groups;
    }
    for(const auto* name:{"ot216_ta","ot216_ra"}) for(const auto* failure:{"set","commit","commit_applied","query_error","query_short","data_error","data_short","data_corrupt"}) {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);offer.kind=2;mock::fault(failure,name);ConfigurationFrame out{};
        CHECK(backend.execute(source.value.context,request(offer),out));CHECK(payload(out).status==3&&mock::handles.empty());no_b_traffic();++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);(void)offer;
        source.value.now_ms=60100;backend.observe();CHECK(mock::handles.empty()&&mock::splits==0);CHECK(backend.close());++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);(void)offer;ConfigurationFrame out{};
        CHECK(backend.execute(source.value.context,request({}),out));CHECK(payload(out).status==4&&mock::handles.empty()&&mock::splits==0);++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);offer.kind=2;ConfigurationFrame out{};
        mock::storage_callback=[&]{if(mock::writes["ot216_ra"]>0)source.value.now_ms=60100;};
        CHECK(backend.execute(source.value.context,request(offer),out));CHECK(payload(out).status==3);mock::storage_callback=[]{};
        CHECK(mock::splits==1&&mock::handles.empty());a_retired();no_b_traffic();++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};
        mock::random_callback=[&]{mock::controller=ESP_BT_CONTROLLER_STATUS_IDLE;};
        CHECK(backend.execute(source.value.context,request({}),out));CHECK(payload(out).status==4&&mock::handles.empty());no_b_traffic();++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);ConfigurationFrame out{};bool once=false;
        mock::random_callback=[&]{if(!once){once=true;CHECK(!backend.close());}};
        CHECK(!backend.execute(source.value.context,request({}),out));CHECK(mock::handles.empty());no_b_traffic();++groups;
    }
    {
        mock::reset();Source source;ConfirmationEvaluationBackend backend(source);auto offer=get_offer(backend,source);offer.kind=2;ConfigurationFrame out{};bool once=false;
        source.callback=[&]{if(!once){once=true;backend.observe();}};
        CHECK(!backend.execute(source.value.context,request(offer),out));CHECK(mock::handles.empty()&&mock::splits==0);++groups;
    }
    std::cout<<"PASS "<<groups<<" actual protected confirmation backend groups\n";
}
