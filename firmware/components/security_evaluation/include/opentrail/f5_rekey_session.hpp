#pragma once
// D-124/I-072: public recovery history never reconstructs a traffic lease.
// One serialized conversation owner holds a candidate and immutable old/current
// lease slots. Target composition supplies D-112 trust and independent RF arm.
#include "opentrail/evaluation_confirmation_owner.hpp"
#include "opentrail/enrollment_recovery_owner.hpp"
#include "opentrail/f5_rekey_journal.hpp"
#include "opentrail/packet_v1_selected_core.hpp"
#include <sodium/crypto_kdf_hkdf_sha256.h>
#include <sodium/crypto_scalarmult_curve25519.h>
#include <algorithm>
#include <array>
#include <limits>

namespace opentrail::identity::roster { struct Roster; }
namespace opentrail::identity::roster::qr { struct Descriptor; }

namespace opentrail::security_evaluation {
class F5RekeySession final {
public:
    using Binding=protocol::v1::selected_packet_detail::Binding;
    using Journal=F5RekeyJournal;
    using Phase=Journal::Phase;
    using ProofKind=Journal::ProofKind;
    struct Stores {
        persistence::PersistentStorage& identity;
        persistence::PersistentStorage& journal;
        persistence::PersistentStorage& membership;
        persistence::PersistentStorage& evidence;
        persistence::PersistentStorage& binding;
        persistence::PersistentStorage& state;
        persistence::PersistentStorage& state_tail;
    };
    // The target read-view verifies the current D-112 peer-confirmed transaction,
    // including real retained identity/proof. Both boards return its same digest.
    // seed_allowed checks actual packet inventory before absent/incomplete BASE.
    struct Trust {
        bool(*read)(void*,InvitationKey&){};
        void* context{};
        bool(*seed_allowed)(void*){};
        bool(*guard)(void*){};
    };
    struct Frame {std::array<std::uint8_t,255> bytes{};std::size_t size{};};
    static constexpr std::uint64_t kAttemptMs=60000;
    F5RekeySession(Stores stores,security::SecureRandomSource& random,std::uint64_t conversation,
                   std::uint64_t(*clock)(void*),void* context,Trust trust={nullptr,nullptr,nullptr,nullptr})
        :stores_(stores),random_(random),identity_(stores.identity,random),journal_(stores.state,stores.state_tail),
         conversation_(conversation),clock_(clock),clock_context_(context),trust_(trust){}
    ~F5RekeySession(){close();}
    F5RekeySession(const F5RekeySession&)=delete;
    F5RekeySession& operator=(const F5RekeySession&)=delete;

    bool initialize(){
        Guard guard(*this);if(!guard)return false;
        if(initialized_ || !clock_ || !conversation_ || !trust_.read || !trust_.guard)return fail();
        initialized_=true;
        std::array<persistence::PersistentStorage*,7> owners{&stores_.identity,&stores_.journal,&stores_.membership,&stores_.evidence,&stores_.binding,&stores_.state,&stores_.state_tail};
        for(unsigned i=0;i<owners.size();++i)for(unsigned j=0;j<i;++j)if(owners[i]==owners[j])return fail();
        InvitationKey before{};if(!source_fingerprint(before))return fail();
        if(identity_.load_existing()!=EnrollmentIdentityStore::LoadResult::ready || !identity_.public_key(local_))return fail();
        EnrollmentCommitCoordinator original(stores_.journal);EnrollmentCommitContext r{};
        if(!original.initialize() || original.state()!=EnrollmentJournalState::active_committed || !original.read_public(r))return fail();
        if(local_==r.identities.initiator)member_=1;
        else if(local_==r.identities.responder)member_=2;
        else return fail();
        peer_=member_==1?r.identities.responder:r.identities.initiator;
        role_=member_==1?InvitationRole::initiator:InvitationRole::responder;
        if(!EnrollmentRecoveryOwner::inspect(stores_.journal,stores_.membership,stores_.evidence,stores_.binding,
            role_,local_,conversation_,retained_) || retained_.stage!=3 || retained_.layout!=3 ||
            !trust_.read(trust_.context,trust_scope_) || !nonzero(trust_scope_))return fail();
        constexpr char label[]="OpenTrail F5 outside conversation v2";
        std::array<std::uint8_t,sizeof(label)-1+32+32+8+32+32> bytes{};std::size_t at=0;
        auto append=[&](const std::uint8_t* p,std::size_t n){std::memcpy(bytes.data()+at,p,n);at+=n;};
        append(reinterpret_cast<const std::uint8_t*>(label),sizeof(label)-1);
        append(r.identities.initiator.data(),32);append(r.identities.responder.data(),32);
        put(bytes.data()+at,conversation_,8);at+=8;append(trust_scope_.data(),32);append(r.evidence_digest.data(),32);
        crypto_hash_sha256(scope_.data(),bytes.data(),bytes.size());
        baseline_.phase=Phase::history;baseline_.kind=ProofKind::base;baseline_.used=r.epoch;
        baseline_.proof_size=64;std::memcpy(baseline_.proof.data(),scope_.data(),32);
        std::memcpy(baseline_.proof.data()+32,r.evidence_digest.data(),32);
        if(!source_fingerprint(source_digest_) || source_digest_!=before)return fail();
        last_now_=clock_(clock_context_);source_ready_=true;
        if(!journal_.initialize(baseline_,&validate_record,this,&journal_guard,this,&validate_partial))return fail();
        journal_ready_=true;
        if(journal_.empty() && (!trust_.seed_allowed || !trust_.seed_allowed(trust_.context) || !journal_.seed(&journal_guard,this)))return fail();
        if(!journal_.load(record_))return fail();
        return current();
    }
    // A repeated unanswered local start returns identical bytes/allocation and
    // preserves the original deadline. Either persistent member can initiate.
    bool start(Frame& output){
        output={};Guard guard(*this);if(!guard || !current())return false;
        expire();
        if(candidate_.present && candidate_.phase==1 && candidate_.initiating_member==member_){output=candidate_.start;return true;}
        if(candidate_.present && candidate_.phase!=7)return false;
        return begin_start(output);
    }
    bool service(Frame& output){
        output={};Guard guard(*this);if(!guard || !current())return false;
        expire();
        if(nudge_ && last_now_>=next_nudge_){
            nudge_=false;
            if(last_now_>std::numeric_limits<std::uint64_t>::max()-1000)return fail();
            next_nudge_=last_now_+1000;
            if(candidate_.present && candidate_.phase==1 && is_attempt_initiator()){
                output=candidate_.start;return true;
            }
            discard_candidate();return begin_start(output);
        }
        return true;
    }
    bool receive(const Frame& input,Frame& output){
        const Frame frame=input;output={};Guard guard(*this);
        if(!guard || !current() || frame.size>frame.bytes.size() || frame.size<8)return false;
        expire();
        if(candidate_.present && same(frame,candidate_.last_input)){
            output=candidate_.last_output;return !output.size || frame_eligible_internal(output);
        }
        if(header(frame,1))return receive_start(frame,output);
        if(!candidate_.present || !candidate_current() || !header(frame,frame.bytes[5]) || frame.bytes[6]!=candidate_.initiating_member)return false;
        Frame staged{};const auto kind=frame.bytes[5];bool accepted=false;
        if(is_attempt_initiator() && candidate_.phase==1 && kind==2){
            if(!verify_offer(frame,2,candidate_.initiating_member,hash(candidate_.start)) || offer_epoch(frame)!=candidate_.target)return false;
            candidate_.reply=frame;transcript(candidate_);
            Frame confirm{};if(!make_control(3,confirm) || !prepare_binding(confirm))return fail();
            candidate_.phase=3;staged=confirm;accepted=true;
        }else if(!is_attempt_initiator() && candidate_.phase==2 && kind==3){
            transcript(candidate_);
            if(!verify_control(frame,3))return false;
            if(!prepare_binding(frame) || !save_phase(Phase::activation_possible) || !make_control(4,staged))return fail();
            candidate_.phase=4;accepted=true;
        }else if(is_attempt_initiator() && candidate_.phase==3 && kind==4){
            if(!verify_control(frame,4))return false;
            if(!save_phase(Phase::activation_possible) || !make_control(5,staged))return fail();
            candidate_.phase=5;accepted=true;
        }else if(!is_attempt_initiator() && candidate_.phase==4 && kind==5){
            if(!verify_control(frame,5))return false;
            if(!save_phase(Phase::committed) || !make_control(6,staged))return fail();
            candidate_.phase=6;accepted=true;
        }else if(is_attempt_initiator() && candidate_.phase==5 && kind==6){
            if(!verify_control(frame,6))return false;
            if(!save_phase(Phase::committed) || !make_control(7,staged) || !bind_packets())return fail();
            candidate_.phase=7;accepted=true;
        }else if(!is_attempt_initiator() && candidate_.phase==6 && kind==7){
            if(!verify_control(frame,7))return false;
            if(!bind_packets())return fail();
            candidate_.phase=7;accepted=true;
        }
        if(!accepted)return false;
        candidate_.last_input=frame;candidate_.last_output=staged;candidate_.outgoing=staged;
        if(!current() || !candidate_current())return false;
        output=staged;return true;
    }
    bool ready(){Guard guard(*this);return guard && current() && active_lease()!=nullptr;}
    bool ready_for_scope(const InvitationKey& peer, const InvitationKey& scope, std::uint64_t purpose) {
        Guard guard(*this);
        return guard && peer_ == peer && trust_scope_ == scope && conversation_ == purpose &&
            current() && active_lease() != nullptr;
    }
    bool initiator()const{return candidate_.present?candidate_.initiating_member==member_:member_==1;}
    bool failed()const{return failed_;}
    bool candidate_active()const{return candidate_.present && candidate_.phase!=7;}
    bool can_start()const{return initialized_ && !failed_ && !candidate_active() && active_lease_const()==nullptr;}
    std::uint64_t generation()const{return record_.allocation;}
    std::uint64_t active_generation()const{const auto* l=active_lease_const();return l?l->allocation:0;}
    std::uint64_t lease_id()const{const auto* l=active_lease_const();return l?l->id:0;}
    std::uint32_t used_epoch()const{return record_.used;}
    std::uint32_t committed_epoch()const{return record_.committed;}
    std::uint32_t epoch()const{const auto* l=active_lease_const();return l?l->epoch:candidate_.present?candidate_.target:record_.used;}
    std::uint64_t attempt_deadline()const{return candidate_.present?candidate_.deadline:0;}
    std::uint64_t conversation()const{return conversation_;}
    const InvitationKey& packet_owner()const{return scope_;}
    Binding& data_tx(){auto* l=active_lease();return l?l->bindings[0]:empty_[0];}
    Binding& ack_tx(){auto* l=active_lease();return l?l->bindings[1]:empty_[1];}
    Binding& data_rx(){auto* l=active_lease();return l?l->bindings[2]:empty_[2];}
    Binding& ack_rx(){auto* l=active_lease();return l?l->bindings[3]:empty_[3];}
    // Destroy borrowers of these Binding references before recycling the lease
    // slots. A copied binding's callback can only address its original lease.
    void release_retired_leases(){
        if(busy_){fail();return;}
        for(auto& l:leases_)if(l.occupied && l.retired){wipe_lease(l);l=Lease{};}
    }
    // Independent local RF-arm loss retires volatile traffic without losing
    // the trusted public conversation. Peer input cannot call or reverse this.
    void retire_traffic(){
        if(busy_){fail();return;}
        discard_candidate();nudge_=false;for(auto& lease:leases_)retire(lease);
    }
    bool frame_eligible(const Frame& f){Guard guard(*this);return guard && current() && frame_eligible_internal(f);}
    bool packet_eligible(std::uint64_t id,std::uint64_t allocation){Guard guard(*this);auto* l=active_lease();return guard && current() && l && l->id==id && l->allocation==allocation;}
    void close(){failed_=true;discard_candidate();for(auto& l:leases_)retire(l);identity_.retire();journal_.retire();}
    // Target app-task adapter reuses this live store. Only typed initial roster
    // signing is available, independent of the short-lived RF traffic lease.
    bool read_group_roster_key(InvitationKey&);
    bool sign_initial_group_roster(const identity::roster::Roster&, std::array<std::uint8_t, 64>&);
    // F8 Invite while this session holds the identity (I-101): one exact
    // canonical descriptor with this board as admin.
    bool sign_invite_descriptor(const identity::roster::qr::Descriptor&, std::array<std::uint8_t, 64>&);
    // F8 QR join over the radio: QR session frames only, and the D-172 static.
    bool sign_qr_join_frame(const std::uint8_t*, std::size_t, std::array<std::uint8_t, 64>&);
    bool sign_successor_group_roster(const identity::roster::Roster& previous, const identity::roster::Roster& next,
                                     std::array<std::uint8_t, 64>&);
    bool use_group_join_static(bool (*use)(void*, const std::array<std::uint8_t, 32>&), void* context);
private:
    struct Candidate {
        bool present{};unsigned phase{},initiating_member{};std::uint32_t target{};std::uint64_t allocation{},deadline{};
        InvitationKey secret{},shared{},ephemeral{},challenge{},transcript{};
        std::array<std::uint8_t,16> boot{};
        Frame start{},reply{},confirm{},last_input{},last_output{},outgoing{};
    };
    struct Lease {
        bool occupied{},retired{};std::uint64_t id{},allocation{};std::uint32_t epoch{};
        InvitationKey transcript{};std::array<Binding,4> bindings{};
    };
    class Guard {
    public:
        explicit Guard(F5RekeySession& s):owner_(s){if(s.busy_){s.fail();return;}if(s.failed_)return;s.busy_=true;admitted_=true;}
        ~Guard(){if(admitted_)owner_.busy_=false;}
        explicit operator bool()const{return admitted_;}
    private:F5RekeySession& owner_;bool admitted_{};
    };
    static void put(std::uint8_t* p,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[n-1-i]=static_cast<std::uint8_t>(v>>(8*i));}
    static std::uint64_t get(const std::uint8_t* p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|p[i];return v;}
    template<std::size_t N>static bool nonzero(const std::array<std::uint8_t,N>& b){for(auto c:b)if(c)return true;return false;}
    static bool same(const Frame& a,const Frame& b){return a.size && a.size==b.size && sodium_memcmp(a.bytes.data(),b.bytes.data(),a.size)==0;}
    static InvitationKey hash(const Frame& f){InvitationKey h{};crypto_hash_sha256(h.data(),f.bytes.data(),f.size);return h;}
    static bool header(const Frame& f,unsigned kind){return f.size>=8 && f.bytes[0]=='O' && f.bytes[1]=='T' && f.bytes[2]=='F' && f.bytes[3]=='5' && f.bytes[4]==2 && f.bytes[5]==kind && (f.bytes[6]==1 || f.bytes[6]==2) && f.bytes[7]==0;}
    static std::uint32_t offer_epoch(const Frame& f){return static_cast<std::uint32_t>(get(f.bytes.data()+16,4));}
    const InvitationKey& member_key(unsigned member)const{return member==1?retained_.record.identities.initiator:retained_.record.identities.responder;}
    bool is_attempt_initiator()const{return candidate_.initiating_member==member_;}
    bool sample_clock(){
        const auto now=clock_(clock_context_);if(now<last_now_)return fail();last_now_=now;return !failed_;
    }
    bool source_fingerprint(InvitationKey& out){
        crypto_hash_sha256_state state{};if(crypto_hash_sha256_init(&state)!=0)return false;
        std::array<persistence::PersistentStorage*,5> stores{&stores_.identity,&stores_.journal,&stores_.membership,&stores_.evidence,&stores_.binding};
        std::array<std::uint8_t,64> bytes{};
        for(auto* store:stores)for(unsigned d=0;d<5;++d)for(unsigned slot=0;slot<2;++slot){
            const auto r=store->read_slot(static_cast<persistence::StorageDomain>(d),slot,{bytes.data(),bytes.size()});
            if(failed_ || !r.read() || r.bytes_read!=bytes.size()){
                sodium_memzero(bytes.data(),bytes.size());sodium_memzero(&state,sizeof state);return false;
            }
            crypto_hash_sha256_update(&state,bytes.data(),bytes.size());
        }
        sodium_memzero(bytes.data(),bytes.size());const bool ok=crypto_hash_sha256_final(&state,out.data())==0;
        sodium_memzero(&state,sizeof state);return ok && !failed_;
    }
    bool current(bool with_journal=true){
        if(failed_ || !source_ready_ || !sample_clock())return false;
        if(!trust_.guard(trust_.context))return fail();
        if(with_journal){
            InvitationKey actual{},trusted{};
            if(!source_fingerprint(actual) || actual!=source_digest_ || !trust_.read(trust_.context,trusted) ||
                trusted!=trust_scope_ || (journal_ready_ && !journal_.exact()))return fail();
        }
        if(!sample_clock())return false;
        if(!trust_.guard(trust_.context))return fail();
        return !failed_;
    }
    static bool journal_guard(void* p){auto& s=*static_cast<F5RekeySession*>(p);return s.current(false) && (!s.candidate_.present || s.last_now_<s.candidate_.deadline);}
    bool save(const Journal::Record& r){if(!current() || !journal_.update(r,&journal_guard,this) || !journal_.load(record_))return false;return current();}
    bool reserve(){if(record_.allocation>=Journal::kMaxAllocation)return false;auto next=record_;++next.allocation;return save(next);}
    bool candidate_current(){return candidate_.present && last_now_<candidate_.deadline && candidate_.target>=record_.used && candidate_.allocation<=record_.allocation;}
    void discard_candidate(){sodium_memzero(candidate_.secret.data(),32);sodium_memzero(candidate_.shared.data(),32);candidate_=Candidate{};}
    void expire(){if(candidate_.present && last_now_>=candidate_.deadline)discard_candidate();}
    bool fresh_material(){
        if(random_.state()!=security::EntropyState::ready)return false;
        for(auto part: {std::pair<std::uint8_t*,std::size_t>{candidate_.secret.data(),32},{candidate_.challenge.data(),32},{candidate_.boot.data(),16}}){
            const auto r=random_.fill(part.first,part.second);if(!r.ok() || r.bytes_written!=part.second || random_.state()!=security::EntropyState::ready)return false;
        }
        return nonzero(candidate_.challenge) && nonzero(candidate_.boot) && crypto_scalarmult_curve25519_base(candidate_.ephemeral.data(),candidate_.secret.data())==0 && current();
    }
    bool begin_start(Frame& output){
        if(record_.used==std::numeric_limits<std::uint32_t>::max() || !reserve())return fail();
        discard_candidate();candidate_.present=true;candidate_.phase=1;candidate_.initiating_member=member_;candidate_.target=record_.used+1;
        candidate_.allocation=record_.allocation;
        if(last_now_>std::numeric_limits<std::uint64_t>::max()-kAttemptMs)return fail();
        candidate_.deadline=last_now_+kAttemptMs;
        if(!fresh_material() || !make_offer(1,{},candidate_.start))return fail();
        candidate_.outgoing=candidate_.start;output=candidate_.start;return true;
    }
    bool make_offer(unsigned kind,const InvitationKey& prior,Frame& f){
        f={};f.size=236;auto* b=f.bytes.data();std::memcpy(b,"OTF5",4);b[4]=2;b[5]=static_cast<std::uint8_t>(kind);b[6]=static_cast<std::uint8_t>(candidate_.initiating_member);
        put(b+8,conversation_,8);put(b+16,candidate_.target,4);put(b+20,candidate_.allocation,8);
        std::memcpy(b+28,scope_.data(),32);std::memcpy(b+60,prior.data(),32);std::memcpy(b+92,candidate_.ephemeral.data(),32);
        std::memcpy(b+124,candidate_.boot.data(),16);std::memcpy(b+140,candidate_.challenge.data(),32);
        std::array<std::uint8_t,64> signature{};if(!identity_.sign(b,172,signature))return false;
        std::memcpy(b+172,signature.data(),64);return current() && candidate_current();
    }
    bool verify_offer(const Frame& f,unsigned kind,unsigned initiating,const InvitationKey& prior,bool signature=true)const{
        const auto* b=f.bytes.data();const auto signer=kind==1?initiating:3-initiating;
        return f.size==236 && header(f,kind) && b[6]==initiating && get(b+8,8)==conversation_ && offer_epoch(f)!=0 &&
            get(b+20,8)>0 && get(b+20,8)<=Journal::kMaxAllocation && sodium_memcmp(b+28,scope_.data(),32)==0 &&
            sodium_memcmp(b+60,prior.data(),32)==0 && nonzero_bytes(b+92,32) && nonzero_bytes(b+124,16) && nonzero_bytes(b+140,32) &&
            (!signature || crypto_sign_verify_detached(b+172,b,172,member_key(signer).data())==0);
    }
    static bool nonzero_bytes(const std::uint8_t* p,std::size_t n){for(std::size_t i=0;i<n;++i)if(p[i])return true;return false;}
    bool receive_start(const Frame& frame,Frame& output){
        const auto received_at=last_now_;
        const auto initiating=frame.bytes[6];
        if(initiating!=3-member_ || !verify_offer(frame,1,initiating,{}))return false;
        const auto e=offer_epoch(frame);
        if(e<=record_.used){
            if(!same(frame,last_stale_)){last_stale_=frame;nudge_=true;}
            return true;
        }
        if(std::uint64_t(e)>std::uint64_t(record_.used)+2)return false;
        if(candidate_.present && candidate_.phase==1 && is_attempt_initiator()){
            if(candidate_.target>e || (candidate_.target==e &&
                std::lexicographical_compare(local_.begin(),local_.end(),peer_.begin(),peer_.end())))return false;
        }
        if(candidate_.present && candidate_.phase==2 && candidate_.target==e &&
            candidate_.initiating_member==initiating && get(frame.bytes.data()+20,8)<=get(candidate_.start.bytes.data()+20,8))return false;
        if(last_now_<next_answer_)return false;
        // Keep the active lease through an ordinary START and this public write.
        // U+2 is signed D-074 evidence: burn its intermediate number durably
        // before a reply can escape, and retire any overtaken old lane.
        if(received_at>std::numeric_limits<std::uint64_t>::max()-kAttemptMs || record_.allocation>=Journal::kMaxAllocation)return fail();
        discard_candidate();candidate_.present=true;candidate_.phase=2;candidate_.initiating_member=initiating;
        candidate_.target=e;candidate_.deadline=received_at+kAttemptMs;candidate_.start=frame;
        if(!reserve())return fail();
        candidate_.allocation=record_.allocation;
        auto observed=record_;observed.phase=Phase::observed;observed.kind=ProofKind::start;
        observed.used=e-1;observed.proof_size=236;observed.proof.fill(0);
        std::memcpy(observed.proof.data(),frame.bytes.data(),236);
        if(observed.used>record_.used)retire_active();
        if(!save(observed) || !fresh_material() || !make_offer(2,hash(frame),candidate_.reply))return fail();
        next_answer_=last_now_>std::numeric_limits<std::uint64_t>::max()-1000?std::numeric_limits<std::uint64_t>::max():last_now_+1000;
        candidate_.last_input=frame;candidate_.last_output=candidate_.reply;candidate_.outgoing=candidate_.reply;
        output=candidate_.reply;return true;
    }
    static void transcript(Candidate& c){
        std::array<std::uint8_t,64> joined{};const auto a=hash(c.start),b=hash(c.reply);
        std::memcpy(joined.data(),a.data(),32);std::memcpy(joined.data()+32,b.data(),32);
        crypto_hash_sha256(c.transcript.data(),joined.data(),joined.size());
    }
    static Frame control_bytes(unsigned kind,unsigned initiating,const InvitationKey& digest){
        Frame out{};out.size=104;std::memcpy(out.bytes.data(),"OTF5",4);out.bytes[4]=2;
        out.bytes[5]=static_cast<std::uint8_t>(kind);out.bytes[6]=static_cast<std::uint8_t>(initiating);
        std::memcpy(out.bytes.data()+8,digest.data(),32);return out;
    }
    bool make_control(unsigned kind,Frame& out){
        out=control_bytes(kind,candidate_.initiating_member,candidate_.transcript);
        std::array<std::uint8_t,64> sig{};if(!identity_.sign(out.bytes.data(),40,sig))return false;
        std::memcpy(out.bytes.data()+40,sig.data(),64);return current() && candidate_current();
    }
    bool verify_control(const Frame& in,unsigned kind)const{
        return in.size==104 && header(in,kind) && in.bytes[6]==candidate_.initiating_member &&
            sodium_memcmp(in.bytes.data()+8,candidate_.transcript.data(),32)==0 &&
            crypto_sign_verify_detached(in.bytes.data()+40,in.bytes.data(),40,peer_.data())==0;
    }
    bool prepare_binding(const Frame& confirm){
        transcript(candidate_);candidate_.confirm=confirm;
        if(candidate_.target<=record_.used || std::uint64_t(candidate_.target)!=std::uint64_t(record_.used)+1)return false;
        const auto& remote=is_attempt_initiator()?candidate_.reply:candidate_.start;
        if(crypto_scalarmult_curve25519(candidate_.shared.data(),candidate_.secret.data(),remote.bytes.data()+92)!=0)return false;
        sodium_memzero(candidate_.secret.data(),candidate_.secret.size());
        auto prepared=record_;prepared.phase=Phase::prepared;prepared.kind=ProofKind::binding;
        prepared.used=candidate_.target;prepared.proof_size=536;prepared.proof.fill(0);
        std::memcpy(prepared.proof.data(),candidate_.start.bytes.data(),236);
        std::memcpy(prepared.proof.data()+236,candidate_.reply.bytes.data(),236);
        std::memcpy(prepared.proof.data()+472,confirm.bytes.data()+40,64);
        // Fresh proof, rather than an unauthenticated/replayed offer, is the
        // handoff boundary. No old frame may newly launch beyond this point.
        retire_active();return save(prepared);
    }
    bool save_phase(Phase phase){
        if(!candidate_current() || record_.kind!=ProofKind::binding || record_.used!=candidate_.target ||
            std::memcmp(record_.proof.data(),candidate_.start.bytes.data(),236)!=0 ||
            std::memcmp(record_.proof.data()+236,candidate_.reply.bytes.data(),236)!=0 ||
            std::memcmp(record_.proof.data()+472,candidate_.confirm.bytes.data()+40,64)!=0)return false;
        auto next=record_;next.phase=phase;if(phase==Phase::committed)next.committed=next.used;
        return save(next);
    }
    static bool validate_record(void* p,const Journal::Record& r){return static_cast<F5RekeySession*>(p)->valid_record(r);}
    static bool validate_partial(void* p,const Journal::Record& r,unsigned known){
        auto& s=*static_cast<F5RekeySession*>(p);
        if(r.kind==ProofKind::base)return true;
        Frame a{},b{};a.size=236;
        if(known>=236){
            std::memcpy(a.bytes.data(),r.proof.data(),236);
            if(!s.verify_offer(a,1,a.bytes[6],{}))return false;
        }
        if(r.kind==ProofKind::binding && known>=472){
            b.size=236;std::memcpy(b.bytes.data(),r.proof.data()+236,236);
            if(!s.verify_offer(b,2,a.bytes[6],hash(a)) || offer_epoch(a)!=offer_epoch(b))return false;
        }
        return known<r.proof_size || s.valid_record(r);
    }
    bool valid_record(const Journal::Record& r)const{
        if(r.allocation>Journal::kMaxAllocation || r.used<retained_.record.epoch || r.committed>r.used || r.flags)return false;
        if(r.kind==ProofKind::base){
            return r.phase==Phase::history && r.used==baseline_.used && r.committed==0 && r.proof_size==64 &&
                std::memcmp(r.proof.data(),baseline_.proof.data(),64)==0;
        }
        Frame a{},b{};a.size=236;std::memcpy(a.bytes.data(),r.proof.data(),236);
        const auto initiating=a.bytes[6];
        if(initiating!=1 && initiating!=2)return false;
        const bool known=proof_cached(r);
        if(!verify_offer(a,1,initiating,{},!known))return false;
        const auto e=offer_epoch(a);
        if(r.kind==ProofKind::start){
            const bool ok=r.phase==Phase::observed && r.proof_size==236 && r.allocation>0 && initiating==3-member_ &&
                e>0 && std::uint64_t(r.used)+1==e;
            if(ok && !known)remember_proof(r);
            return ok;
        }
        if(r.kind!=ProofKind::binding || r.proof_size!=536 || r.allocation==0 || r.used!=e ||
            (r.phase!=Phase::prepared && r.phase!=Phase::activation_possible && r.phase!=Phase::committed))return false;
        b.size=236;std::memcpy(b.bytes.data(),r.proof.data()+236,236);
        if(!verify_offer(b,2,initiating,hash(a),!known) || offer_epoch(b)!=e)return false;
        const auto& local_offer=member_==initiating?a:b;
        if(get(local_offer.bytes.data()+20,8)>r.allocation)return false;
        Candidate proof{};proof.start=a;proof.reply=b;transcript(proof);
        auto confirm=control_bytes(3,initiating,proof.transcript);
        if(!known && crypto_sign_verify_detached(r.proof.data()+472,confirm.bytes.data(),40,member_key(initiating).data())!=0)return false;
        const bool ok=r.phase==Phase::committed?r.committed==r.used:r.committed<r.used;
        if(ok && !known)remember_proof(r);
            return ok;
    }
    struct ProofCache {bool used{};ProofKind kind{};std::uint16_t size{};std::array<std::uint8_t,536> proof{};};
    bool proof_cached(const Journal::Record& r)const{
        for(const auto& cached:verified_proofs_)if(cached.used && cached.kind==r.kind && cached.size==r.proof_size && cached.proof==r.proof)return true;
        return false;
    }
    void remember_proof(const Journal::Record& r)const{
        auto& cached=verified_proofs_[next_proof_++%verified_proofs_.size()];
        cached.used=true;cached.kind=r.kind;cached.size=r.proof_size;cached.proof=r.proof;
    }
    bool derive(Lease& lease,Binding& b,const InvitationKey& split,const InvitationKey& sender,const InvitationKey& recipient,unsigned direction,protocol::v1::Type purpose){
        constexpr char label[]="OpenTrail Packet-v1 session binding HKDF-SHA256 v1";
        std::array<std::uint8_t,sizeof(label)-1+32+32+32+8+4+1+1> info{};std::size_t at=0;
        auto append=[&](const std::uint8_t* p,std::size_t n){std::memcpy(info.data()+at,p,n);at+=n;};
        append(reinterpret_cast<const std::uint8_t*>(label),sizeof(label)-1);append(scope_.data(),32);append(sender.data(),32);append(recipient.data(),32);
        put(info.data()+at,conversation_,8);at+=8;put(info.data()+at,lease.epoch,4);at+=4;
        info[at++]=static_cast<std::uint8_t>(direction);info[at++]=static_cast<std::uint8_t>(purpose);
        InvitationKey prk{};std::array<std::uint8_t,52> material{};
        const bool ok=crypto_kdf_hkdf_sha256_extract(prk.data(),lease.transcript.data(),32,split.data(),32)==0 &&
            crypto_kdf_hkdf_sha256_expand(material.data(),material.size(),reinterpret_cast<const char*>(info.data()),info.size(),prk.data())==0;
        if(ok){
            b.context.sender=sender;b.context.recipient=recipient;b.context.transcript=lease.transcript;
            b.context.sender_alias=get(sender.data(),8);b.context.recipient_alias=get(recipient.data(),8);
            b.context.group=conversation_;b.context.epoch=lease.epoch;b.context.direction=static_cast<std::uint8_t>(direction);b.purpose=purpose;
            std::memcpy(b.key.data(),material.data(),32);std::memcpy(b.domain.data(),material.data()+32,16);b.lease_domain=b.domain;
            std::memcpy(b.prefix.data(),material.data()+48,4);
            b.active=true;b.validity=&lease_gate;b.validity_context=this;b.lease_id=lease.id;b.close_owner=&close_lease;
        }
        sodium_memzero(prk.data(),32);sodium_memzero(material.data(),material.size());return ok;
    }
    bool bind_packets(){
        if(record_.phase!=Phase::committed || record_.used!=candidate_.target || record_.committed!=candidate_.target ||
            lease_counter_==std::numeric_limits<std::uint64_t>::max())return false;
        Lease* next=nullptr;for(auto& l:leases_)if(!l.occupied){next=&l;break;}if(!next)return false;
        next->occupied=true;next->id=++lease_counter_;next->allocation=candidate_.allocation;
        next->epoch=candidate_.target;next->transcript=candidate_.transcript;
        InvitationKey prk{},ab{},ba{};
        constexpr char l1[]="OpenTrail F5 outside conversation initiating to answering v2";
        constexpr char l2[]="OpenTrail F5 outside conversation answering to initiating v2";
        const bool i=is_attempt_initiator();
        const bool ok=crypto_kdf_hkdf_sha256_extract(prk.data(),candidate_.transcript.data(),32,candidate_.shared.data(),32)==0 &&
            crypto_kdf_hkdf_sha256_expand(ab.data(),32,l1,sizeof(l1)-1,prk.data())==0 &&
            crypto_kdf_hkdf_sha256_expand(ba.data(),32,l2,sizeof(l2)-1,prk.data())==0 &&
            derive(*next,next->bindings[0],i?ab:ba,local_,peer_,member_-1,protocol::v1::Type::data) &&
            derive(*next,next->bindings[1],i?ab:ba,local_,peer_,member_-1,protocol::v1::Type::ack) &&
            derive(*next,next->bindings[2],i?ba:ab,peer_,local_,2-member_,protocol::v1::Type::data) &&
            derive(*next,next->bindings[3],i?ba:ab,peer_,local_,2-member_,protocol::v1::Type::ack);
        sodium_memzero(prk.data(),32);sodium_memzero(ab.data(),32);sodium_memzero(ba.data(),32);sodium_memzero(candidate_.shared.data(),32);
        if(!ok){retire(*next);return false;}
        if(!current() || !candidate_current()){retire(*next);return false;}
        active_index_=static_cast<unsigned>(next-leases_.data());next_answer_=0;return true;
    }
    Lease* active_lease(){return active_index_<leases_.size() && leases_[active_index_].occupied && !leases_[active_index_].retired?&leases_[active_index_]:nullptr;}
    const Lease* active_lease_const()const{return active_index_<leases_.size() && leases_[active_index_].occupied && !leases_[active_index_].retired?&leases_[active_index_]:nullptr;}
    static void wipe_lease(Lease& l){for(auto& b:l.bindings)sodium_memzero(&b,sizeof b);}
    static void retire(Lease& l){l.retired=true;wipe_lease(l);}
    void retire_active(){if(auto* l=active_lease())retire(*l);}
    Lease* find_lease(std::uint64_t id){
        for(auto& lease:leases_)if(lease.occupied && lease.id==id)return &lease;
        return nullptr;
    }
    static void close_lease(void* p,std::uint64_t id){
        if(!p)return;
        if(auto* lease=static_cast<F5RekeySession*>(p)->find_lease(id))retire(*lease);
    }
    static bool lease_gate(void* p,std::uint64_t id,std::uint64_t){
        if(!p)return false;
        auto& s=*static_cast<F5RekeySession*>(p);
        auto* lease=s.find_lease(id);if(!lease)return false;
        Guard guard(s);
        return guard && !lease->retired && s.current() && !lease->retired && lease->id==id && lease->epoch>=s.record_.used;
    }
    bool frame_eligible_internal(const Frame& f)const{
        return candidate_.present && last_now_<candidate_.deadline && candidate_.target>=record_.used && same(f,candidate_.outgoing);
    }
    bool fail(){failed_=true;discard_candidate();for(auto& l:leases_)retire(l);return false;}
    Stores stores_;security::SecureRandomSource& random_;EnrollmentIdentityStore identity_;Journal journal_;
    std::uint64_t conversation_{},(*clock_)(void*){},last_now_{},next_answer_{},next_nudge_{},lease_counter_{};
    void* clock_context_{};Trust trust_;unsigned member_{},active_index_{2};InvitationRole role_{};
    bool initialized_{},source_ready_{},journal_ready_{},failed_{},busy_{},nudge_{};
    EnrollmentRecoveryState retained_{};InvitationKey local_{},peer_{},trust_scope_{},scope_{},source_digest_{};
    mutable std::array<ProofCache,4> verified_proofs_{};mutable unsigned next_proof_{};
    Journal::Record baseline_{},record_{};Frame last_stale_{};Candidate candidate_{};std::array<Lease,2> leases_{};std::array<Binding,4> empty_{};
};
} // namespace opentrail::security_evaluation
