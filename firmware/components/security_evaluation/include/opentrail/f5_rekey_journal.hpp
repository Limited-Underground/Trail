#pragma once
// Public-only F5 restart history. Two portions form ONE 640-byte bank; neither
// portion commits independently. The retained proof never restores a key, lane,
// peer readiness, delivery receipt or a right to transmit. Exclusive serialized
// storage owners and the existing all-domain reset boundary remain required.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include "opentrail/persistent_storage.hpp"
#include <sodium/crypto_hash_sha256.h>

namespace opentrail::security_evaluation {
class F5RekeyJournal final {
public:
    // This is a logical generation. The target budgets physical packet slots.
    // Keep the upper bound representable by the existing packet message ID.
    static constexpr std::uint64_t kMaxAllocation = std::numeric_limits<std::uint32_t>::max();
    enum class Phase : std::uint8_t { history, observed, prepared, activation_possible, committed };
    enum class ProofKind : std::uint8_t { base, start, binding };
    struct Record {
        std::uint64_t allocation{};
        std::uint32_t used{}, committed{};
        Phase phase{Phase::history};
        ProofKind kind{ProofKind::base};
        std::uint8_t flags{};
        std::uint16_t proof_size{};
        std::array<std::uint8_t,536> proof{};
    };
    using Bank=std::array<std::uint8_t,640>;
    using Validator=bool(*)(void*,const Record&);
    using PartialValidator=bool(*)(void*,const Record&,unsigned);
    using Guard=bool(*)(void*);
    F5RekeyJournal(persistence::PersistentStorage& first,persistence::PersistentStorage& second)
        :portions_{&first,&second}{}
    F5RekeyJournal(const F5RekeyJournal&)=delete;
    F5RekeyJournal& operator=(const F5RekeyJournal&)=delete;
    // Initial baseline is pinned to the verified original pairing. Initialization
    // reads only. seed() is the exact first write, before allocation or an offer.
    bool initialize(const Record& baseline,Validator validator,void* validator_context,
                    Guard guard=nullptr,void* guard_context=nullptr,PartialValidator partial_validator=nullptr){
        const auto base=baseline;
        return guarded(guard,guard_context,[&]{
            if(initialized_ || portions_[0]==portions_[1] || !validator ||
               !valid_record(base) || base.kind!=ProofKind::base || base.phase!=Phase::history ||
               base.allocation || base.committed)return refuse();
            initialized_=true;validator_=validator;partial_validator_=partial_validator;validator_context_=validator_context;base_=base;
            if(!validated(base) || !read(retained_))return refuse();
            if(blank(retained_[0]) && blank(retained_[1])){record_=base;empty_=true;return true;}
            Record records[2]{};std::uint64_t sequences[2]{};bool valid[2]{};
            for(unsigned i=0;i<2;++i){
                valid[i]=decode(retained_[i],records[i],sequences[i]) &&
                    (sequences[i]-1)%2==i && validated(records[i]);
                if(failed_)return false;
            }
            if(!valid[0] && !valid[1]){
                if(blank(retained_[1]) && initial_prefix(retained_[0])){
                    record_=base;empty_=true;return true;
                }
                return refuse();
            }
            const unsigned latest=!valid[0]?1:!valid[1]?0:sequences[1]>sequences[0]?1:0;
            const unsigned older=1-latest;
            if(valid[older] && (sequences[latest]!=sequences[older]+1 || !follows(records[older],records[latest])))return refuse();
            if(!valid[older] && !blank(retained_[older]) &&
               !interrupted(retained_[older],records[latest],sequences[latest]))return refuse();
            record_=records[latest];sequence_=sequences[latest];current_slot_=latest;empty_=false;
            return true;
        });
    }
    bool seed(Guard guard=nullptr,void* context=nullptr){
        return guarded(guard,context,[&]{
            if(!initialized_ || !exact_impl())return false;
            return !empty_ || persist(base_);
        });
    }
    bool load(Record& output){
        Record staged{};
        const bool ok=operation([&]{
            if(!initialized_ || !exact_impl() || !validated(record_))return false;
            staged=record_;return true;
        });
        if(ok)output=staged;
        return ok;
    }
    bool update(const Record& input,Guard guard=nullptr,void* context=nullptr){
        const Record next=input;
        return guarded(guard,context,[&]{
            if(!initialized_ || empty_ || !exact_impl() || !validated(next) || !follows(record_,next))return false;
            if(equal(record_,next))return true;
            return persist(next);
        });
    }
    bool exact(){return operation([&]{return initialized_ && exact_impl();});}
    void retire(){failed_=true;}
    bool failed()const{return failed_;}
    bool empty()const{return empty_;}
    std::uint64_t sequence()const{return sequence_;}
    static bool equal(const Record& a,const Record& b){
        return a.allocation==b.allocation && a.used==b.used && a.committed==b.committed &&
            a.phase==b.phase && a.kind==b.kind && a.flags==b.flags &&
            a.proof_size==b.proof_size && a.proof==b.proof;
    }
    static bool valid_record(const Record& r){
        if(!r.used || r.committed>r.used || r.allocation>kMaxAllocation || r.flags ||
           static_cast<unsigned>(r.phase)>static_cast<unsigned>(Phase::committed) ||
           static_cast<unsigned>(r.kind)>static_cast<unsigned>(ProofKind::binding))return false;
        const unsigned bytes=r.kind==ProofKind::base?64:r.kind==ProofKind::start?236:536;
        if(r.proof_size!=bytes)return false;
        for(unsigned i=bytes;i<r.proof.size();++i)if(r.proof[i])return false;
        if(r.kind==ProofKind::base)return r.phase==Phase::history && !r.committed;
        if(!r.allocation)return false;
        if(r.kind==ProofKind::start)return r.phase==Phase::observed;
        if(r.phase!=Phase::prepared && r.phase!=Phase::activation_possible && r.phase!=Phase::committed)return false;
        return r.phase!=Phase::committed || r.committed==r.used;
    }
    static bool follows(const Record& a,const Record& b){
        if(!valid_record(a) || !valid_record(b) || b.used<a.used || b.committed<a.committed ||
           b.allocation<a.allocation)return false;
        if(equal(a,b))return true;
        // Reservation changes A only. Proof can still name an older allocation.
        auto allocated=a;
        if(a.allocation<kMaxAllocation){++allocated.allocation;if(equal(allocated,b))return true;}
        if(b.allocation!=a.allocation || !b.allocation || b.committed!=a.committed){
            // A local COMMIT raises C only for the exact already-saved binding.
            return b.allocation==a.allocation && b.used==a.used && b.committed==b.used &&
                a.phase==Phase::activation_possible && b.phase==Phase::committed && same_proof(a,b);
        }
        if(b.kind==ProofKind::start && b.phase==Phase::observed){
            // Ordinary start changes no used floor; U+2 burns one intermediate.
            return b.used==a.used || (a.used!=std::numeric_limits<std::uint32_t>::max() && b.used==a.used+1);
        }
        if(b.kind==ProofKind::binding && b.phase==Phase::prepared){
            return a.used!=std::numeric_limits<std::uint32_t>::max() && b.used==a.used+1;
        }
        return b.used==a.used && same_proof(a,b) && a.phase==Phase::prepared && b.phase==Phase::activation_possible;
    }
    // Big-endian integers and explicit bytes; no C++ object representation.
    static Bank encode(const Record& r,std::uint64_t sequence){
        if(r.proof_size>r.proof.size())return {};
        Bank b{};std::memcpy(b.data(),"OTFJ",4);b[4]=2;b[5]=static_cast<std::uint8_t>(r.phase);
        b[6]=static_cast<std::uint8_t>(r.kind);b[7]=r.flags;
        put(b.data()+8,sequence,8);put(b.data()+16,r.allocation,8);put(b.data()+24,r.used,4);put(b.data()+28,r.committed,4);
        put(b.data()+32,r.proof_size,2);std::memcpy(b.data()+40,r.proof.data(),r.proof_size);
        crypto_hash_sha256(b.data()+604,b.data(),604);std::memcpy(b.data()+636,kMarker.data(),4);
        return b;
    }
    static bool decode(const Bank& b,Record& output,std::uint64_t& sequence){
        if(std::memcmp(b.data(),"OTFJ",4)!=0 || b[4]!=2 || marker(b)!=kMarker)return false;
        Record r{};r.phase=static_cast<Phase>(b[5]);r.kind=static_cast<ProofKind>(b[6]);r.flags=b[7];
        const auto seq=get(b.data()+8,8);r.allocation=get(b.data()+16,8);r.used=static_cast<std::uint32_t>(get(b.data()+24,4));
        r.committed=static_cast<std::uint32_t>(get(b.data()+28,4));r.proof_size=static_cast<std::uint16_t>(get(b.data()+32,2));
        if(r.proof_size>r.proof.size())return false;
        std::memcpy(r.proof.data(),b.data()+40,r.proof_size);
        if(!seq || !valid_record(r) || b!=encode(r,seq))return false;
        output=r;sequence=seq;return true;
    }
private:
    using Media=std::array<Bank,2>;
    static constexpr std::array<std::uint8_t,4> kMarker{'F','5','J','2'};
    static_assert(persistence::kStorageDomainCount==5 && persistence::kPersistentSlotCount==2 && persistence::kPersistentSlotBytes==64);
    static void put(std::uint8_t* p,std::uint64_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[bytes-1-i]=static_cast<std::uint8_t>(n>>(8*i));}
    static std::uint64_t get(const std::uint8_t* p,unsigned bytes){std::uint64_t n=0;for(unsigned i=0;i<bytes;++i)n=(n<<8)|p[i];return n;}
    static bool same_proof(const Record& a,const Record& b){return a.kind==b.kind && a.proof_size==b.proof_size && a.flags==b.flags && a.proof==b.proof;}
    static bool blank(const Bank& b){for(auto c:b)if(c!=0xff)return false;return true;}
    static std::array<std::uint8_t,4> marker(const Bank& b){return {b[636],b[637],b[638],b[639]};}
    static bool incomplete_marker(const std::array<std::uint8_t,4>& m){
        for(unsigned installed=0;installed<4;++installed){
            std::array<std::uint8_t,4> v{};v.fill(0xff);for(unsigned i=0;i<installed;++i)v[i]=kMarker[i];
            if(m==v)return true;
            for(unsigned cleared=1;cleared<=4;++cleared){v[cleared-1]=0;if(m==v)return true;}
        }
        return false;
    }
    static unsigned payload_floor(const std::array<std::uint8_t,4>& m){
        for(auto c:m)if(c!=0 && c!=0xff)return 636;
        return 0;
    }
    static bool prefix_equal(const Bank& observed,const Bank& expected,unsigned floor){
        unsigned minimum=floor;
        for(unsigned i=0;i<636;++i)if(observed[i]!=0xff && minimum<i+1)minimum=i+1;
        for(unsigned i=0;i<minimum;++i)if(observed[i]!=expected[i])return false;
        return true;
    }
    bool initial_prefix(const Bank& b){
        return incomplete_marker(marker(b)) && prefix_equal(b,encode(base_,1),payload_floor(marker(b)));
    }
    bool validated(const Record& r){return live() && validator_ && validator_(validator_context_,r) && live();}
    // Incomplete new payload has no authority. Validate every available fixed
    // byte, transition and full proof; unavailable signature suffixes are never
    // interpreted as signed facts. Only the current complete bank supplies U/C/A.
    bool candidate_prefix(const Bank& b,const Record& previous,std::uint64_t seq){
        const auto m=marker(b);if(!incomplete_marker(m))return false;
        unsigned prefix=payload_floor(m);
        for(unsigned i=0;i<636;++i)if(b[i]!=0xff && prefix<i+1)prefix=i+1;
        auto match=[&](Record next,bool variable){
            if(!follows(previous,next))return false;
            auto encoded=encode(next,seq);
            if(!variable)return prefix_equal(b,encoded,prefix);
            // Metadata and padding are determined before opaque proof bytes.
            for(unsigned i=0;i<40 && i<prefix;++i)if(b[i]!=encoded[i])return false;
            const unsigned proof_end=40+next.proof_size;
            for(unsigned i=proof_end;i<604 && i<prefix;++i)if(b[i])return false;
            const auto checked=std::min<unsigned>(prefix,proof_end);
            if(checked>40)std::memcpy(next.proof.data(),b.data()+40,checked-40);
            // Every stored offer has the explicit v2 role/header and epoch.
            for(unsigned offer=0;offer<(next.kind==ProofKind::binding?2U:1U);++offer){
                const unsigned at=40+offer*236;
                const std::array<std::uint8_t,6> header{'O','T','F','5',2,static_cast<std::uint8_t>(offer?2:1)};
                for(unsigned i=0;i<header.size() && at+i<prefix;++i)if(b[at+i]!=header[i])return false;
                if(at+6<prefix && b[at+6]!=1 && b[at+6]!=2)return false;
                if(at+7<prefix && b[at+7])return false;
                const std::uint64_t epoch=next.kind==ProofKind::start?std::uint64_t(next.used)+1:next.used;
                for(unsigned i=0;i<4 && at+16+i<prefix;++i)if(b[at+16+i]!=static_cast<std::uint8_t>(epoch>>(8*(3-i))))return false;
                for(unsigned i=0;i<32 && at+28+i<prefix;++i)if(b[at+28+i]!=base_.proof[i])return false;
            }
            if(partial_validator_ && (!live() || !partial_validator_(validator_context_,next,checked>40?checked-40:0) || !live()))return false;
            if(prefix>=proof_end){
                if(!validated(next))return false;
                encoded=encode(next,seq);
                return std::memcmp(encoded.data(),b.data(),prefix)==0;
            }
            return true;
        };
        auto next=previous;
        if(previous.allocation<kMaxAllocation){++next.allocation;if(match(next,false))return true;}
        for(unsigned burn=0;burn<2;++burn){
            if(burn && previous.used==std::numeric_limits<std::uint32_t>::max())continue;
            next=previous;next.used+=burn;next.phase=Phase::observed;next.kind=ProofKind::start;next.proof_size=236;next.proof.fill(0);
            if(match(next,true))return true;
        }
        if(previous.used!=std::numeric_limits<std::uint32_t>::max()){
            next=previous;++next.used;next.phase=Phase::prepared;next.kind=ProofKind::binding;next.proof_size=536;next.proof.fill(0);
            if(match(next,true))return true;
        }
        next=previous;next.phase=Phase::activation_possible;if(match(next,false))return true;
        next=previous;next.phase=Phase::committed;next.committed=next.used;return match(next,false);
    }
    bool interrupted(const Bank& b,const Record& current,std::uint64_t sequence){
        if(marker(b)==kMarker)return false; // Completed damage never falls back.
        if(sequence!=std::numeric_limits<std::uint64_t>::max() && candidate_prefix(b,current,sequence+1))return true;
        if(sequence<=1)return false;
        // Marker retirement of an intact predecessor is independently checked.
        auto repaired=b;std::memcpy(repaired.data()+636,kMarker.data(),4);
        Record prior{};std::uint64_t prior_sequence{};
        if(decode(repaired,prior,prior_sequence) && validated(prior) && prior_sequence+1==sequence && follows(prior,current)){
            for(unsigned count=1;count<=4;++count){auto expected=repaired;std::memset(expected.data()+636,0,count);if(expected==b)return true;}
        }
        // Erase is ordered over the ten tuples only after marker retirement.
        // A surviving suffix is old, non-authoritative payload: retain it until
        // replacement, require its canonical padding, and never import its floor.
        if(marker(b)!=std::array<std::uint8_t,4>{})return false;
        for(unsigned erased=1;erased<10;++erased){
            bool prefix=true;for(unsigned i=0;i<erased*64;++i)if(b[i]!=0xff)prefix=false;
            if(!prefix)continue;
            bool padding=true;for(unsigned i=std::max<unsigned>(erased*64,576);i<604;++i)if(b[i])padding=false;
            if(padding)return true;
        }
        return false;
    }
    bool refuse(){failed_=true;return false;}
    bool live(){return !failed_ && (!guard_ || guard_(guard_context_)) && !failed_;}
    template<class Action> bool operation(Action action){
        if(busy_)return refuse();
        if(failed_)return false;
        busy_=true;const bool ok=action();busy_=false;return ok && !failed_;
    }
    template<class Action> bool guarded(Guard guard,void* context,Action action){
        if(busy_)return refuse();
        if(failed_)return false;
        guard_=guard;guard_context_=context;const bool ok=operation([&]{return live() && action() && live();});
        guard_=nullptr;guard_context_=nullptr;return ok;
    }
    bool read(Media& m){
        for(unsigned slot=0;slot<2;++slot)for(unsigned portion=0;portion<2;++portion)for(unsigned d=0;d<5;++d){
            if(!live())return false;
            const auto r=portions_[portion]->read_slot(static_cast<persistence::StorageDomain>(d),slot,{m[slot].data()+portion*320+d*64,64});
            if(!live() || !r.read() || r.bytes_read!=64)return false;
        }
        return true;
    }
    bool exact_impl(){Media observed{};return read(observed) && observed==retained_ ? true:refuse();}
    bool sync(unsigned tuple,unsigned slot){return live() && portions_[tuple/5]->sync_slot(static_cast<persistence::StorageDomain>(tuple%5),slot)==persistence::StorageError::none && live();}
    bool write(unsigned tuple,unsigned slot,unsigned offset,const std::uint8_t* bytes,unsigned count){
        return live() && portions_[tuple/5]->write_slot(static_cast<persistence::StorageDomain>(tuple%5),slot,offset,{bytes,count})==persistence::StorageError::none && live();
    }
    bool persist(const Record& next){
        if(sequence_==std::numeric_limits<std::uint64_t>::max())return refuse();
        const unsigned target=empty_?0:1-current_slot_;
        const auto encoded=encode(next,sequence_+1);auto expected=retained_;Media observed{};
        if(!blank(expected[target])){
            constexpr std::array<std::uint8_t,4> retired{};
            if(!write(9,target,60,retired.data(),4) || !sync(9,target))return refuse();
            std::memcpy(expected[target].data()+636,retired.data(),4);
            if(!read(observed) || observed!=expected)return refuse();
        }
        for(unsigned t=0;t<10;++t){
            if(!live() || portions_[t/5]->erase_slot(static_cast<persistence::StorageDomain>(t%5),target)!=persistence::StorageError::none || !live() || !sync(t,target))return refuse();
            std::memset(expected[target].data()+t*64,0xff,64);
        }
        if(!read(observed) || observed!=expected)return refuse();
        for(unsigned t=0;t<10;++t){
            const unsigned bytes=t==9?60:64;
            if(!write(t,target,0,encoded.data()+t*64,bytes) || !sync(t,target))return refuse();
            std::memcpy(expected[target].data()+t*64,encoded.data()+t*64,bytes);
        }
        if(!read(observed) || observed!=expected)return refuse();
        if(!write(9,target,60,encoded.data()+636,4) || !sync(9,target))return refuse();
        expected[target]=encoded;
        if(!read(observed) || observed!=expected || !validated(next))return refuse();
        retained_=expected;record_=next;current_slot_=target;++sequence_;empty_=false;return true;
    }
    std::array<persistence::PersistentStorage*,2> portions_;
    Media retained_{};Record record_{},base_{};
    Validator validator_{};PartialValidator partial_validator_{};void* validator_context_{};Guard guard_{};void* guard_context_{};
    std::uint64_t sequence_{};unsigned current_slot_{};bool initialized_{},empty_{},failed_{},busy_{};
};
} // namespace opentrail::security_evaluation
