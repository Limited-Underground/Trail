#include "opentrail/evaluation_replay_store.hpp"
#include "memory_persistent_storage.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace opentrail::persistence;
using namespace opentrail::security_eval;
using opentrail::persistence::test_support::MemoryPersistentStorage;
constexpr auto domain = StorageDomain::outbound_counter_state;
using Store = EvaluationReplayStore;
using Error = ReplayError;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

Store::Context context() {
    Store::Context value{};
    for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::uint8_t>(i + 1);
    return value;
}

// Models an operation which reaches persistent storage but reports failure.
class AmbiguousStorage final : public PersistentStorage {
public:
    MemoryPersistentStorage memory;
    int fail_after{-1};
    int calls{0};
    bool short_read{false};
    bool wrong_readback{false};
    StorageError result(StorageError value) {
        return calls++ == fail_after ? StorageError::io_failure : value;
    }
    StorageReadResult read_slot(StorageDomain d, std::size_t s, MutableStorageByteView v) override {
        auto r = memory.read_slot(d, s, v);
        if (short_read && r.bytes_read) --r.bytes_read;
        if (wrong_readback && v.size) v.data[0] ^= 1;
        return r;
    }
    StorageError erase_slot(StorageDomain d, std::size_t s) override {
        return result(memory.erase_slot(d, s));
    }
    StorageError write_slot(StorageDomain d, std::size_t s, std::size_t o, StorageByteView v) override {
        return result(memory.write_slot(d, s, o, v));
    }
    StorageError sync_slot(StorageDomain d, std::size_t s) override {
        return result(memory.sync_slot(d, s));
    }
};

void rewrite_checksum(std::array<std::uint8_t, 64>& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < 56; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0U);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) bytes[56 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
}

int main() {
    const auto ctx = context();
    unsigned groups = 0;
    {
        MemoryPersistentStorage m; Store s(m);
        CHECK(s.accept_authenticated(1) == Error::not_started);
        CHECK(s.retire() == Error::not_started);
        CHECK(s.start(ctx, false) == Error::fresh_not_allowed);
        CHECK(m.counters(domain).erases == 0); ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m);
        CHECK(s.start({}, true) == Error::invalid_context);
        CHECK(m.counters(domain).reads == 0); ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m);
        CHECK(s.start(ctx, true) == Error::none);
        CHECK(s.accept_authenticated(7) == Error::none);
        const auto writes = m.counters(domain).writes;
        CHECK(s.accept_authenticated(7) == Error::replay);
        CHECK(s.accept_authenticated(6) == Error::replay);
        CHECK(s.accept_authenticated(0) == Error::invalid_counter);
        CHECK(m.counters(domain).writes == writes && s.ready());
        CHECK(s.accept_authenticated(UINT64_C(0x100000001)) == Error::none);
        CHECK(s.accept_authenticated(UINT64_MAX) == Error::none);
        Store restart(m); CHECK(restart.start(ctx, false) == Error::none);
        CHECK(restart.high_water() == UINT64_MAX);
        CHECK(restart.accept_authenticated(UINT64_MAX) == Error::replay); ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
        auto other = ctx; other[31] ^= 1;
        auto writes = m.counters(domain).writes;
        Store restart(m); CHECK(restart.start(other, true) == Error::context_mismatch);
        CHECK(m.counters(domain).writes == writes); ++groups;
    }
    {
        MemoryPersistentStorage baseline; Store s(baseline); CHECK(s.start(ctx, true) == Error::none);
        for (std::size_t offset : {0U, 4U, 5U, 6U, 8U, 12U, 44U, 52U, 56U, 60U}) {
            auto m = baseline; m.corrupt_byte(domain, 0, offset, 0x80);
            Store restart(m); CHECK(restart.start(ctx, true) == Error::corrupt_record);
            CHECK(restart.failed());
        }
        ++groups;
    }
    {
        MemoryPersistentStorage m; m.fail_next_read(); Store s(m);
        CHECK(s.start(ctx, true) == Error::storage_failure);
        CHECK(s.accept_authenticated(1) == Error::storage_failure);
        AmbiguousStorage short_storage; short_storage.short_read = true; Store short_store(short_storage);
        CHECK(short_store.start(ctx, true) == Error::storage_failure); ++groups;
    }
    {
        // Every interrupted mutation boundary, both initial and successor writes.
        for (bool initialized : {false, true}) for (std::size_t cut = 0; cut < 5; ++cut) {
            MemoryPersistentStorage m; Store s(m);
            if (initialized) CHECK(s.start(ctx, true) == Error::none);
            m.arm_power_loss_after(cut);
            auto e = initialized ? s.accept_authenticated(19) : s.start(ctx, true);
            CHECK(e != Error::none && s.failed());
            CHECK(s.accept_authenticated(20) != Error::none);
            m.clear_fault(); Store restart(m);
            const auto resumed = restart.start(ctx, false);
            if (resumed == Error::none) {
                CHECK(initialized || restart.high_water() == 0);
                if (restart.high_water() == 19) CHECK(restart.accept_authenticated(19) == Error::replay);
                else CHECK(restart.high_water() == 0); // No plaintext was released for 19.
            } else CHECK(restart.failed());
        }
        ++groups;
    }
    {
        for (int cut = 0; cut < 5; ++cut) {
            AmbiguousStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
            m.calls = 0; m.fail_after = cut;
            CHECK(s.accept_authenticated(29) == Error::storage_failure && s.failed());
            m.fail_after = -1; Store restart(m);
            auto e = restart.start(ctx, false);
            if (cut >= 3) {
                CHECK(e == Error::none && restart.high_water() == 29);
                CHECK(restart.accept_authenticated(29) == Error::replay);
            } else CHECK(e != Error::none || restart.high_water() == 0);
        }
        ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
        m.fail_next_read(); CHECK(s.accept_authenticated(31) == Error::verification_failure);
        CHECK(s.failed()); Store restart(m); CHECK(restart.start(ctx, false) == Error::none);
        CHECK(restart.accept_authenticated(31) == Error::replay); ++groups;
    }
    {
        AmbiguousStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
        m.wrong_readback = true;
        CHECK(s.accept_authenticated(32) == Error::verification_failure && s.failed()); ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
        CHECK(s.accept_authenticated(41) == Error::none);
        CHECK(s.retire() == Error::none && s.retired());
        CHECK(s.accept_authenticated(42) == Error::retired);
        Store restart(m); CHECK(restart.start(ctx, true) == Error::retired);
        auto other = ctx; other[0] ^= 1;
        Store rekey(m); CHECK(rekey.start(other, true) == Error::context_mismatch); ++groups;
    }
    {
        for (int cut = 0; cut < 5; ++cut) {
            AmbiguousStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
            m.calls = 0; m.fail_after = cut;
            CHECK(s.retire() == Error::storage_failure && s.failed());
            CHECK(s.accept_authenticated(1) != Error::none);
            m.fail_after = -1; Store restart(m);
            auto e = restart.start(ctx, false);
            if (cut >= 3) CHECK(e == Error::retired);
        }
        ++groups;
    }
    {
        MemoryPersistentStorage m; Store s(m); CHECK(s.start(ctx, true) == Error::none);
        auto bytes = m.slot_bytes(domain, 0);
        for (std::size_t i = 8; i < 12; ++i) bytes[i] = 0xFF;
        rewrite_checksum(bytes); m.seed_slot(domain, 0, bytes);
        Store restart(m); CHECK(restart.start(ctx, false) == Error::none);
        auto writes = m.counters(domain).writes;
        CHECK(restart.accept_authenticated(1) == Error::generation_exhausted);
        CHECK(m.counters(domain).writes == writes); ++groups;
    }
    {
        MemoryPersistentStorage baseline; Store s(baseline); CHECK(s.start(ctx, true) == Error::none);
        const auto original = baseline.slot_bytes(domain, 0);
        for (unsigned mode = 0; mode < 4; ++mode) {
            auto m = baseline; auto bytes = original;
            if (mode == 0) bytes[44] = 1; // Conflicting equal generation.
            if (mode == 1) bytes[8] = 3; // Nonconsecutive generation.
            if (mode == 2) bytes[8] = 2; // Equal counter in active successor.
            if (mode == 3) bytes[12] ^= 1; // Wrong context in other slot.
            rewrite_checksum(bytes); m.seed_slot(domain, 1, bytes);
            Store restart(m); CHECK(restart.start(ctx, true) != Error::none);
        }
        ++groups;
    }
    {
        MemoryPersistentStorage m;
        auto untouched = m.slot_bytes(StorageDomain::secret_material, 0);
        Store s(m); CHECK(s.start(ctx, true) == Error::none);
        CHECK(s.accept_authenticated(1) == Error::none && s.retire() == Error::none);
        CHECK(m.slot_bytes(StorageDomain::secret_material, 0) == untouched);
        CHECK(m.counters(StorageDomain::secret_material).writes == 0); ++groups;
    }
    std::printf("PASS %u durable replay store groups\n", groups);
}
