#pragma once
// D-113 link-test owner: retained exact outside-group conversation -> fresh
// volatile keys. R2 provenance remains read-only. This is a development carrier,
// not production admission. One serialized application owner and disjoint stores
// are required; reset must erase this state and all Packet-v1 stores together.
#include "opentrail/evaluation_confirmation_owner.hpp"
#include "opentrail/enrollment_recovery_owner.hpp"
#include "opentrail/packet_v1_selected_core.hpp"
#include <sodium/crypto_kdf_hkdf_sha256.h>
#include <sodium/crypto_scalarmult_curve25519.h>

namespace opentrail::security_evaluation {
class F5RekeySession final {
public:
    using Binding=protocol::v1::selected_packet_detail::Binding;
    struct Stores {
        persistence::PersistentStorage& identity;
        persistence::PersistentStorage& journal;
        persistence::PersistentStorage& membership;
        persistence::PersistentStorage& evidence;
        persistence::PersistentStorage& binding;
        persistence::PersistentStorage& state;
    };
    struct Frame { std::array<std::uint8_t,255> bytes{};std::size_t size{}; };
    F5RekeySession(Stores stores,security::SecureRandomSource& random,std::uint64_t conversation,
                   std::uint64_t(*clock)(void*),void* context)
        :stores_(stores),random_(random),identity_(stores.identity,random),conversation_(conversation),clock_(clock),clock_context_(context){}
    ~F5RekeySession(){close();}
    F5RekeySession(const F5RekeySession&)=delete;
    F5RekeySession& operator=(const F5RekeySession&)=delete;
    // Reads actual R2 committed provenance, then reserves a durable session
    // incarnation. A partial/uncertain F5 journal is never repaired here.
    bool initialize(){
        Guard guard(*this);if(!guard)return false;
        if(initialized_ || !clock_ || !conversation_)return fail();
        initialized_=true;
        std::array<persistence::PersistentStorage*,6> owners{&stores_.identity,&stores_.journal,&stores_.membership,&stores_.evidence,&stores_.binding,&stores_.state};
        for(unsigned i=0;i<owners.size();++i)for(unsigned j=0;j<i;++j)if(owners[i]==owners[j])return fail();
        if(identity_.load_existing()!=EnrollmentIdentityStore::LoadResult::ready || !identity_.public_key(local_))return fail();
        EnrollmentCommitCoordinator journal(stores_.journal);EnrollmentCommitContext record{};
        if(!journal.initialize() || journal.state()!=EnrollmentJournalState::active_committed || !journal.read_public(record))return fail();
        if(local_==record.identities.initiator)role_=InvitationRole::initiator;
        else if(local_==record.identities.responder)role_=InvitationRole::responder;
        else return fail();
        peer_=initiator()?record.identities.responder:record.identities.initiator;
        if(!EnrollmentRecoveryOwner::inspect(stores_.journal,stores_.membership,stores_.evidence,stores_.binding,
            role_,local_,conversation_,retained_) || retained_.stage!=3 || retained_.layout!=3)return fail();
        // The complete persistent keys define the identities. Explicit domain
        // separation prevents a numerically equal group ID selecting this pair.
        constexpr char label[]="OpenTrail F5 outside conversation v1";
        std::array<std::uint8_t,sizeof(label)-1+32+32+8> name{};
        std::memcpy(name.data(),label,sizeof(label)-1);
        std::memcpy(name.data()+sizeof(label)-1,record.identities.initiator.data(),32);
        std::memcpy(name.data()+sizeof(label)-1+32,record.identities.responder.data(),32);
        put(name.data()+name.size()-8,conversation_,8);
        crypto_hash_sha256(conversation_digest_.data(),name.data(),name.size());
        if(!load_journal())return fail();
        if(stage_!=3 || epoch_==std::numeric_limits<std::uint32_t>::max() || generation_==std::numeric_limits<std::uint64_t>::max())return fail();
        ++generation_;
        if(!persist(3,epoch_))return fail();
        last_now_=clock_(clock_context_);
        return current();
    }
    bool start(Frame& output){
        output={};Guard guard(*this);if(!guard)return false;
        if(!initialized_ || failed_ || started_ || !current())return false;
        started_=true;
        if(!initiator())return true;
        if(!fresh_material() || !make_offer(1,{},start_))return fail();
        phase_=1;output=start_;return true;
    }
    // Duplicate records replay cached exact bytes without another signature,
    // allocation or persistence operation. New peer records never select roots.
    bool receive(const Frame& input,Frame& output){
        const Frame frame=input;output={};Guard guard(*this);if(!guard)return false;
        if(!started_ || failed_ || !current() || frame.size<8 || frame.size>frame.bytes.size())return false;
        if(same(frame,last_input_)){output=last_output_;return true;}
        Frame staged{};bool accepted=false;
        const auto kind=frame.bytes[5];
        if(!header(frame,kind))return false;
        if(!initiator() && phase_==0 && kind==1){
            if(!verify_offer(frame,1,{},peer_))return false;
            start_=frame;
            if(!fresh_material() || !make_offer(2,hash(start_),reply_))return fail();
            phase_=2;staged=reply_;accepted=true;
        }else if(initiator() && phase_==1 && kind==2){
            if(!verify_offer(frame,2,hash(start_),peer_))return false;
            reply_=frame;
            if(!prepare_binding() || !make_control(3,staged))return fail();
            phase_=3;accepted=true;
        }else if(!initiator() && phase_==2 && kind==3){
            reply_digest();
            if(!verify_control(frame,3))return false;
            if(!prepare_binding() || !persist(2,epoch_) || !make_control(4,staged))return fail();
            phase_=4;accepted=true;
        }else if(initiator() && phase_==3 && kind==4){
            if(!verify_control(frame,4))return false;
            if(!persist(2,epoch_) || !make_control(5,staged))return fail();
            phase_=5;accepted=true;
        }else if(!initiator() && phase_==4 && kind==5){
            if(!verify_control(frame,5))return false;
            if(!persist(3,epoch_) || !make_control(6,staged))return fail();
            phase_=6;accepted=true;
        }else if(initiator() && phase_==5 && kind==6){
            if(!verify_control(frame,6))return false;
            if(!persist(3,epoch_) || !make_control(7,staged) || !bind_packets())return fail();
            phase_=7;ready_=true;accepted=true;
        }else if(!initiator() && phase_==6 && kind==7){
            if(!verify_control(frame,7) || !bind_packets())return false;
            phase_=7;ready_=true;accepted=true;
        }
        if(!accepted)return false;
        last_input_=frame;last_output_=staged;output=staged;return current();
    }
    bool ready(){Guard guard(*this);return guard && ready_ && current();}
    bool initiator()const{return role_==InvitationRole::initiator;}
    std::uint64_t generation()const{return generation_;}
    std::uint32_t epoch()const{return epoch_;}
    std::uint64_t conversation()const{return conversation_;}
    Binding& data_tx(){return data_tx_;}Binding& ack_tx(){return ack_tx_;}
    Binding& data_rx(){return data_rx_;}Binding& ack_rx(){return ack_rx_;}
    void close(){failed_=true;ready_=false;identity_.retire();wipe();}
private:
    class Guard {
    public:
        explicit Guard(F5RekeySession& owner):owner_(owner){
            if(owner_.busy_){owner_.fail();return;}
            if(owner_.failed_)return;
            owner_.busy_=true;admitted_=true;
        }
        ~Guard(){if(admitted_)owner_.busy_=false;}
        explicit operator bool()const{return admitted_;}
    private:F5RekeySession& owner_;bool admitted_{};
    };
    using Bytes=std::array<std::uint8_t,64>;
    using Bank=std::array<Bytes,5>;
    static void put(std::uint8_t* p,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[n-1-i]=static_cast<std::uint8_t>(v>>(8*i));}
    static std::uint64_t get(const std::uint8_t* p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|p[i];return v;}
    static InvitationKey hash(const Frame& f){InvitationKey result{};crypto_hash_sha256(result.data(),f.bytes.data(),f.size);return result;}
    static bool same(const Frame& a,const Frame& b){return a.size && a.size==b.size && sodium_memcmp(a.bytes.data(),b.bytes.data(),a.size)==0;}
    static bool blank(const Bank& b){for(const auto& s:b)for(auto c:s)if(c!=0xff)return false;return true;}
    bool snapshot(std::array<Bank,2>& out){
        for(unsigned s=0;s<2;++s)for(unsigned d=0;d<5;++d){auto r=stores_.state.read_slot(static_cast<persistence::StorageDomain>(d),s,{out[s][d].data(),64});if(!r.read() || r.bytes_read!=64)return false;}
        return true;
    }
    bool decode(const Bank& b,std::uint64_t& seq,std::uint64_t& generation,std::uint32_t& epoch,std::uint8_t& stage){
        std::array<std::uint8_t,320> bytes{};for(unsigned d=0;d<5;++d)std::memcpy(bytes.data()+d*64,b[d].data(),64);
        InvitationKey digest{};crypto_hash_sha256(digest.data(),bytes.data(),284);
        if(std::memcmp(bytes.data(),"OTF5",4)!=0 || bytes[4]!=1 || bytes[5]>3 ||
           std::memcmp(bytes.data()+316,"DONE",4)!=0 || sodium_memcmp(digest.data(),bytes.data()+284,32)!=0 ||
           sodium_memcmp(bytes.data()+28,conversation_digest_.data(),32)!=0)return false;
        for(unsigned i=92;i<284;++i)if(bytes[i])return false;
        seq=get(bytes.data()+8,8);generation=get(bytes.data()+16,8);epoch=static_cast<std::uint32_t>(get(bytes.data()+24,4));stage=bytes[5];
        return seq && generation && epoch>=retained_.record.epoch;
    }
    bool load_journal(){
        if(!snapshot(journal_snapshot_))return false;
        const bool a=blank(journal_snapshot_[0]),b=blank(journal_snapshot_[1]);
        if(a&&b){stage_=3;epoch_=retained_.record.epoch;return true;}
        std::array<std::uint64_t,2> seq{},gen{};std::array<std::uint32_t,2> epochs{};std::array<std::uint8_t,2> stages{};
        for(unsigned i=0;i<2;++i)if(!blank(journal_snapshot_[i]) && !decode(journal_snapshot_[i],seq[i],gen[i],epochs[i],stages[i]))return false;
        const unsigned newest=seq[1]>seq[0]?1:0;
        const unsigned prior=1-newest;
        if(seq[prior] && (seq[newest]!=seq[prior]+1 || gen[newest]<gen[prior] || epochs[newest]<epochs[prior]))return false;
        sequence_=seq[newest];generation_=gen[newest];epoch_=epochs[newest];stage_=stages[newest];slot_=newest;
        return stage_==3;
    }
    bool persist(std::uint8_t stage,std::uint32_t epoch){
        if(sequence_==std::numeric_limits<std::uint64_t>::max())return false;
        std::array<Bank,2> current{};if(!snapshot(current) || current!=journal_snapshot_)return false;
        std::array<std::uint8_t,320> bytes{};std::memcpy(bytes.data(),"OTF5",4);bytes[4]=1;bytes[5]=stage;
        put(bytes.data()+8,sequence_+1,8);put(bytes.data()+16,generation_,8);put(bytes.data()+24,epoch,4);
        std::memcpy(bytes.data()+28,conversation_digest_.data(),32);std::memcpy(bytes.data()+60,transcript_.data(),32);
        crypto_hash_sha256(bytes.data()+284,bytes.data(),284);std::memcpy(bytes.data()+316,"DONE",4);
        const unsigned target=sequence_?1-slot_:0;
        for(unsigned d=0;d<5;++d){
            auto domain=static_cast<persistence::StorageDomain>(d);
            if(stores_.state.erase_slot(domain,target)!=persistence::StorageError::none ||
               stores_.state.write_slot(domain,target,0,{bytes.data()+d*64,64})!=persistence::StorageError::none ||
               stores_.state.sync_slot(domain,target)!=persistence::StorageError::none)return false;
            std::memcpy(current[target][d].data(),bytes.data()+d*64,64);
        }
        std::array<Bank,2> verify{};if(!snapshot(verify) || verify!=current)return false;
        journal_snapshot_=verify;++sequence_;slot_=target;stage_=stage;epoch_=epoch;return true;
    }
    bool current(){
        if(failed_ || !initialized_)return false;
        const auto now=clock_(clock_context_);
        if(now<last_now_ || (deadline_ && now>=deadline_))return fail();
        last_now_=now;
        EnrollmentRecoveryState state{};
        std::array<Bank,2> observed{};
        if(!identity_.public_key(local_check_) || local_check_!=local_ ||
           !EnrollmentRecoveryOwner::inspect(stores_.journal,stores_.membership,stores_.evidence,stores_.binding,
               role_,local_,conversation_,state) || !EnrollmentRecoveryOwner::equal(state,retained_) ||
           !snapshot(observed) || observed!=journal_snapshot_)return fail();
        return !failed_;
    }
    bool fresh_material(){
        if(!current() || random_.state()!=security::EntropyState::ready || last_now_>std::numeric_limits<std::uint64_t>::max()-60000)return false;
        deadline_=last_now_+60000;
        std::array<std::uint8_t,80> material{};
        for(unsigned offset=0;offset<material.size();offset+=32){
            const auto size=std::min<std::size_t>(32,material.size()-offset);
            const auto r=random_.fill(material.data()+offset,size);
            if(!r.ok() || r.bytes_written!=size || random_.state()!=security::EntropyState::ready){sodium_memzero(material.data(),material.size());return false;}
        }
        std::memcpy(secret_.data(),material.data(),32);std::memcpy(challenge_.data(),material.data()+32,32);std::memcpy(boot_.data(),material.data()+64,16);
        sodium_memzero(material.data(),material.size());
        if(!invitation_detail::nonzero(challenge_) || !invitation_detail::nonzero(boot_))return false;
        return crypto_scalarmult_curve25519_base(ephemeral_.data(),secret_.data())==0 && current();
    }
    bool header(const Frame& f,unsigned kind)const{
        return f.size>=8 && std::memcmp(f.bytes.data(),"OTF5",4)==0 && f.bytes[4]==1 && f.bytes[5]==kind && f.bytes[6]==0 && f.bytes[7]==0;
    }
    bool make_offer(unsigned kind,const InvitationKey& prior,Frame& f){
        f={};f.size=236;auto* b=f.bytes.data();std::memcpy(b,"OTF5",4);b[4]=1;b[5]=static_cast<std::uint8_t>(kind);
        put(b+8,conversation_,8);put(b+16,epoch_+1,4);put(b+20,generation_,8);
        std::memcpy(b+28,conversation_digest_.data(),32);std::memcpy(b+60,prior.data(),32);
        std::memcpy(b+92,ephemeral_.data(),32);std::memcpy(b+124,boot_.data(),16);std::memcpy(b+140,challenge_.data(),32);
        std::array<std::uint8_t,64> signature{};
        if(!identity_.sign(b,172,signature))return false;
        std::memcpy(b+172,signature.data(),64);return current();
    }
    bool verify_offer(const Frame& f,unsigned kind,const InvitationKey& prior,const InvitationKey& peer){
        const auto* b=f.bytes.data();
        return f.size==236 && header(f,kind) && get(b+8,8)==conversation_ && get(b+16,4)==std::uint64_t(epoch_)+1 && get(b+20,8)!=0 &&
            sodium_memcmp(b+28,conversation_digest_.data(),32)==0 && sodium_memcmp(b+60,prior.data(),32)==0 &&
            crypto_sign_verify_detached(b+172,b,172,peer.data())==0;
    }
    void reply_digest(){
        std::array<std::uint8_t,64> joined{};const auto a=hash(start_),b=hash(reply_);
        std::memcpy(joined.data(),a.data(),32);std::memcpy(joined.data()+32,b.data(),32);crypto_hash_sha256(transcript_.data(),joined.data(),joined.size());
    }
    bool make_control(unsigned kind,Frame& out){
        out={};out.size=104;std::memcpy(out.bytes.data(),"OTF5",4);out.bytes[4]=1;out.bytes[5]=static_cast<std::uint8_t>(kind);
        std::memcpy(out.bytes.data()+8,transcript_.data(),32);std::array<std::uint8_t,64> sig{};
        if(!identity_.sign(out.bytes.data(),40,sig))return false;
        std::memcpy(out.bytes.data()+40,sig.data(),64);return current();
    }
    bool verify_control(const Frame& in,unsigned kind){
        return in.size==104 && header(in,kind) && sodium_memcmp(in.bytes.data()+8,transcript_.data(),32)==0 &&
            crypto_sign_verify_detached(in.bytes.data()+40,in.bytes.data(),40,peer_.data())==0;
    }
    bool prepare_binding(){
        reply_digest();
        const auto& remote=initiator()?reply_:start_;
        if(crypto_scalarmult_curve25519(shared_.data(),secret_.data(),remote.bytes.data()+92)!=0)return false;
        sodium_memzero(secret_.data(),secret_.size());
        return persist(1,epoch_+1) && current();
    }
    bool derive(Binding& b,const InvitationKey& split,const InvitationKey& sender,const InvitationKey& recipient,unsigned direction,protocol::v1::Type purpose){
        constexpr char label[]="OpenTrail Packet-v1 session binding HKDF-SHA256 v1";
        std::array<std::uint8_t,sizeof(label)-1+32+32+32+8+4+1+1> info{};std::size_t at=0;
        auto append=[&](const std::uint8_t* p,std::size_t n){std::memcpy(info.data()+at,p,n);at+=n;};
        append(reinterpret_cast<const std::uint8_t*>(label),sizeof(label)-1);append(conversation_digest_.data(),32);append(sender.data(),32);append(recipient.data(),32);
        put(info.data()+at,conversation_,8);at+=8;put(info.data()+at,epoch_,4);at+=4;info[at++]=static_cast<std::uint8_t>(direction);info[at++]=static_cast<std::uint8_t>(purpose);
        InvitationKey prk{};std::array<std::uint8_t,52> material{};
        const bool ok=crypto_kdf_hkdf_sha256_extract(prk.data(),transcript_.data(),32,split.data(),32)==0 &&
            crypto_kdf_hkdf_sha256_expand(material.data(),material.size(),reinterpret_cast<const char*>(info.data()),info.size(),prk.data())==0;
        if(ok){b.context.sender=sender;b.context.recipient=recipient;b.context.transcript=transcript_;b.context.sender_alias=get(sender.data(),8);b.context.recipient_alias=get(recipient.data(),8);
            b.context.group=conversation_;b.context.epoch=epoch_;b.context.direction=static_cast<std::uint8_t>(direction);b.purpose=purpose;
            std::memcpy(b.key.data(),material.data(),32);std::memcpy(b.domain.data(),material.data()+32,16);b.lease_domain=b.domain;std::memcpy(b.prefix.data(),material.data()+48,4);
            b.active=true;b.validity=&gate;b.validity_context=this;b.close_owner=&close_gate;}
        sodium_memzero(prk.data(),prk.size());sodium_memzero(material.data(),material.size());return ok;
    }
    bool bind_packets(){
        InvitationKey prk{},ab{},ba{};
        constexpr char l1[]="OpenTrail F5 outside conversation A to B v1",l2[]="OpenTrail F5 outside conversation B to A v1";
        const bool ok=crypto_kdf_hkdf_sha256_extract(prk.data(),transcript_.data(),32,shared_.data(),32)==0 &&
            crypto_kdf_hkdf_sha256_expand(ab.data(),32,l1,sizeof(l1)-1,prk.data())==0 &&
            crypto_kdf_hkdf_sha256_expand(ba.data(),32,l2,sizeof(l2)-1,prk.data())==0 &&
            derive(data_tx_,initiator()?ab:ba,local_,peer_,initiator()?0:1,protocol::v1::Type::data) &&
            derive(ack_tx_,initiator()?ab:ba,local_,peer_,initiator()?0:1,protocol::v1::Type::ack) &&
            derive(data_rx_,initiator()?ba:ab,peer_,local_,initiator()?1:0,protocol::v1::Type::data) &&
            derive(ack_rx_,initiator()?ba:ab,peer_,local_,initiator()?1:0,protocol::v1::Type::ack);
        sodium_memzero(prk.data(),32);sodium_memzero(ab.data(),32);sodium_memzero(ba.data(),32);sodium_memzero(shared_.data(),32);
        return ok;
    }
    static bool gate(void* c,std::uint64_t){return static_cast<F5RekeySession*>(c)->ready();}
    static void close_gate(void* c){static_cast<F5RekeySession*>(c)->close();}
    bool fail(){failed_=true;ready_=false;wipe();return false;}
    void wipe(){sodium_memzero(secret_.data(),32);sodium_memzero(shared_.data(),32);sodium_memzero(&data_tx_,sizeof data_tx_);sodium_memzero(&ack_tx_,sizeof ack_tx_);sodium_memzero(&data_rx_,sizeof data_rx_);sodium_memzero(&ack_rx_,sizeof ack_rx_);}
    Stores stores_;security::SecureRandomSource& random_;EnrollmentIdentityStore identity_;
    std::uint64_t conversation_{},(*clock_)(void*){},last_now_{},deadline_{},sequence_{},generation_{};void* clock_context_{};
    std::uint32_t epoch_{};unsigned slot_{},phase_{};std::uint8_t stage_{};InvitationRole role_{};
    bool initialized_{},started_{},failed_{},ready_{},busy_{};
    EnrollmentRecoveryState retained_{};InvitationKey local_{},local_check_{},peer_{},conversation_digest_{},transcript_{},secret_{},shared_{},ephemeral_{},challenge_{};
    std::array<std::uint8_t,16> boot_{};std::array<Bank,2> journal_snapshot_{};
    Frame start_{},reply_{},last_input_{},last_output_{};Binding data_tx_{},ack_tx_{},data_rx_{},ack_rx_{};
};
} // namespace opentrail::security_evaluation
