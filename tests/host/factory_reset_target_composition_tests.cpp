// Actual target ports and executor; only ESP-IDF/NimBLE I/O is simulated.
// Reuse the maintained committed/staged NVS model, without its test main.
#define OPENTRAIL_IDENTITY_NVS_FIXTURE_ONLY
#define ble_store_iterate fixture_ble_store_iterate
#define ble_store_clear fixture_ble_store_clear
#define esp_partition_read fixture_partition_read
#define esp_partition_erase_range fixture_partition_erase
#include "enrollment_identity_nvs_tests.cpp"
#undef ble_store_iterate
#undef ble_store_clear
#undef esp_partition_read
#undef esp_partition_erase_range
#include "opentrail/companion_factory_reset_authority.hpp"
using namespace companion;
int bond_count=1,bond_clear_error=0,bond_read_error=0,partition_error=0;
bool retain_bonds=false,retain_partition=false,partition_read_fails=false;
int clears=0;
int ble_store_iterate(int type,int(*callback)(int,ble_store_value*,void*),void* cookie) {
    if(bond_read_error)return bond_read_error;
    // NimBLE ble_store.c consumes a nonzero callback result as successful stop.
    if(bond_count){ble_store_value value{};(void)callback(type,&value,cookie);}
    return 0;
}
int ble_store_clear(){++clears;if(bond_clear_error)return bond_clear_error;if(!retain_bonds)bond_count=0;return 0;}
esp_err_t esp_partition_read(const esp_partition_t* p,std::size_t o,void* out,std::size_t n) {
    if(partition_read_fails)return ESP_FAIL;
    return fixture_partition_read(p,o,out,n);
}
esp_err_t esp_partition_erase_range(const esp_partition_t* p,std::size_t o,std::size_t n) {
    // Identity must be inaccessible before old counter/queue/map state goes.
    assert(disk[kEnrollmentIdentityStorageNamespace].empty());
    if(partition_error)return partition_error;
    if(retain_partition)return ESP_OK;
    return fixture_partition_erase(p,o,n);
}
void seed() {
    reset();bond_count=1;bond_clear_error=bond_read_error=partition_error=clears=0;
    retain_bonds=retain_partition=partition_read_fails=false;raw_erased=false;
    for(const char* name:{"ot_v1_owner","ot_name_v1","ot_region_v1",kEnrollmentIdentityStorageNamespace})
        disk[name]["orphan_or_user"]={Bytes(5,0x71)};
    for(const char* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"})
        disk[name]["retained_evaluation"]={Bytes(5,0x72)};
    disk["factory_calibration"]["retain"]={Bytes(5,0x42)};
}
struct Target {
    HeltecV4FactoryResetMarkerStorage marker;
    HeltecV4FactoryResetUserDomainStorage user;
    HeltecV4FactoryResetNimbleBondStorage bonds;
    DeviceFactoryResetExecutor executor{marker,user,bonds};
    Target(){bonds.set_store_access_ready(true);}
};
constexpr std::uint64_t receipt=0x1122334455667788;
void owned(Target& t){assert(t.executor.restore().phase==DeviceFactoryResetPhase::idle_old_state);}
void complete(Target& t){assert(t.executor.continue_cleanup().accepted());assert(t.executor.status().reboot_unowned_permitted);assert(t.user.inspect_absence().verified_absent);assert(t.bonds.inspect_empty().verified_absent);assert(!disk["factory_calibration"].empty());
    for(const char* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"})assert(disk[name].empty());
}
void denied(Target& t){assert(!t.executor.status().reboot_unowned_permitted);assert(t.executor.status().phase!=DeviceFactoryResetPhase::idle_unowned);}
int main(){int groups=0;
    // Both caller routes use the same real executor, with receipt only on app.
    for(bool app:{false,true}) {
        seed();{
            Target t;owned(t);
            if(app){CompanionFactoryResetActionAuthority authority(t.executor);
                CompanionActionRequest request{CompanionActionKind::factory_reset,protocol::QuickStatusKind::ok,receipt};
                const auto prepared=authority.prepare_action(request);assert(erases==0&&sets==0);
                assert(authority.commit_action(request,prepared)==CompanionAuthorityError::none);
                assert(authority.status().protected_operations_blocked);
            }else assert(t.executor.begin().accepted());
            denied(t);assert(erases==0&&bond_count==1);complete(t);
        }{
            Target reboot;const auto restored=reboot.executor.restore();assert(restored.accepted());
            if(app){assert(restored.phase==DeviceFactoryResetPhase::completion_receipt_pending);denied(reboot);
                const auto consumed=reboot.executor.consume_completion_receipt();assert(consumed.accepted()&&consumed.reset_receipt==receipt);
                assert(reboot.executor.status().phase==DeviceFactoryResetPhase::idle_unowned);
            }else assert(restored.phase==DeviceFactoryResetPhase::idle_unowned);
            assert(reboot.marker.load().state==DeviceFactoryResetMarkerState::absent);
        }++groups;
    }
    // Prepare then abandon is precommit cancellation, preserving every byte.
    seed();{Target t;owned(t);const auto before=disk;
        {CompanionFactoryResetActionAuthority a(t.executor);CompanionActionRequest r{CompanionActionKind::factory_reset,protocol::QuickStatusKind::ok,receipt};(void)a.prepare_action(r);}
        assert(sets==0&&erases==0&&clears==0&&!raw_erased);assert(disk.size()==before.size());
        for(const auto& ns:before)for(const auto& kv:ns.second)assert(disk[ns.first][kv.first].bytes==kv.second.bytes);
        assert(t.executor.status().old_state_preserved);
    }++groups;
    // Uncertain intent commit: neither possible SDK outcome grants access.
    for(bool applied:{false,true}){seed();{
        Target t;owned(t);error_namespace=kHeltecV4FactoryResetMarkerNamespace;commit_error=ESP_FAIL;commit_applies=applied;
        assert(!t.executor.begin(receipt).accepted());denied(t);assert(erases==0&&clears==0);
    }commit_error=0;commit_applies=true;{
        Target reboot;const auto r=reboot.executor.restore();assert(r.accepted());
        if(applied){assert(r.phase==DeviceFactoryResetPhase::cleanup_required);complete(reboot);}
        else assert(r.phase==DeviceFactoryResetPhase::idle_old_state);
    }++groups;}
    // Exact marker readback corruption contains even though SDK commit succeeds.
    seed();{Target t;owned(t);corrupt_commit=true;assert(!t.executor.begin(receipt).accepted());denied(t);assert(erases==0&&clears==0);}
    corrupt_commit=false;{Target reboot;assert(!reboot.executor.restore().accepted());denied(reboot);}++groups;
    // Each real cleanup port's failure/lying-success retains intent and resumes.
    for(int fault=0;fault<9;++fault){seed();{
        Target t;owned(t);assert(t.executor.begin(receipt).accepted());
        switch(fault){
        case 0:error_namespace="ot_region_v1";erase_error=ESP_FAIL;break;
        case 1:error_namespace=kEnrollmentIdentityStorageNamespace;commit_error=ESP_FAIL;commit_applies=false;break;
        case 2:partition_error=ESP_FAIL;break;
        case 3:retain_partition=true;break;
        case 4:partition_read_fails=true;break;
        case 5:bond_clear_error=1;break;
        case 6:retain_bonds=true;break;
        case 7:bond_read_error=2;break;
        case 8:error_namespace=kHeltecV4FactoryResetMarkerNamespace;commit_error=ESP_FAIL;break;
        }
        assert(!t.executor.continue_cleanup().accepted());denied(t);
        assert(t.executor.status().cleanup_required);
    }
    erase_error=commit_error=partition_error=bond_clear_error=bond_read_error=0;commit_applies=true;
    retain_partition=partition_read_fails=retain_bonds=false;error_namespace.clear();
    {Target reboot;const auto r=reboot.executor.restore();assert(r.accepted());
        if(r.phase==DeviceFactoryResetPhase::cleanup_required)complete(reboot);
        else assert(r.phase==DeviceFactoryResetPhase::completion_receipt_pending);
    }++groups;}
    // Every retained optional-profile namespace participates in absence and
    // idempotent recovery, including names unused by the current application.
    for(const char* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"}) {
        seed();{Target t;owned(t);assert(t.executor.begin(receipt).accepted());
            error_namespace=name;erase_error=ESP_FAIL;
            assert(!t.executor.continue_cleanup().accepted());denied(t);assert(!disk[name].empty());
        }erase_error=0;error_namespace.clear();
        {Target reboot;assert(reboot.executor.restore().phase==DeviceFactoryResetPhase::cleanup_required);complete(reboot);}
        // A lone retained namespace prevents pending-receipt unowned admission.
        disk[name]["residue"]={Bytes(1,3)};
        {Target reboot;assert(reboot.executor.restore().phase==DeviceFactoryResetPhase::cleanup_required);denied(reboot);complete(reboot);}
        {Target reboot;assert(reboot.executor.restore().phase==DeviceFactoryResetPhase::completion_receipt_pending);assert(reboot.executor.consume_completion_receipt().accepted());}
        disk[name]["marker_absent_residue"]={Bytes(1,4)};
        {Target reboot;assert(!reboot.executor.restore().accepted());denied(reboot);}
        ++groups;
    }
    seed();{
        for(const char* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"})disk.erase(name);
        Target t;owned(t);assert(t.executor.begin().accepted());assert(t.executor.continue_cleanup().accepted());
        for(const char* name:{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"})assert(disk.count(name)==0);
    }++groups;
    // A completion receipt with residue must re-enter cleanup, not grant access.
    seed();{Target t;owned(t);assert(t.executor.begin(receipt).accepted());complete(t);}
    disk["ot_region_v1"]["late_residue"]={Bytes(2,9)};
    {Target reboot;assert(reboot.executor.restore().phase==DeviceFactoryResetPhase::cleanup_required);denied(reboot);assert(!reboot.executor.consume_completion_receipt().accepted());complete(reboot);}++groups;
    // Receipt consumption uncertainty never publishes unowned access in-process.
    for(bool applied:{false,true}){seed();{Target t;owned(t);assert(t.executor.begin(receipt).accepted());complete(t);}
        {Target reboot;assert(reboot.executor.restore().phase==DeviceFactoryResetPhase::completion_receipt_pending);
            error_namespace=kHeltecV4FactoryResetMarkerNamespace;commit_error=ESP_FAIL;commit_applies=applied;
            assert(!reboot.executor.consume_completion_receipt().accepted());denied(reboot);
        }commit_error=0;commit_applies=true;
        {Target next;const auto r=next.executor.restore();assert(r.accepted());assert(r.phase==(applied?DeviceFactoryResetPhase::idle_unowned:DeviceFactoryResetPhase::completion_receipt_pending));}
        ++groups;
    }
    assert(handles.empty());std::cout<<"PASS "<<groups<<" actual target reset composition groups\n";
}
