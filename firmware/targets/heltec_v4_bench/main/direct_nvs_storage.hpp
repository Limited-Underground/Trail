#pragma once

#include "pairing_nvs_storage.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::target::heltec_v4_bench {

// One owner of the direct catalog and the eight reserved enrollment/F5 scopes.
// Views borrow the catalog's physical scope slot. The caller drains all
// borrowers before retiring a scope and assigning its slot to another peer.
class DirectNvsStorage final : public persistence::PersistentKvBackend {
public:
    using KvError = persistence::PersistentKvBackendError;
    using Error = persistence::StorageError;
    using Domain = persistence::StorageDomain;
    using Namespace = PairingNvsStorage::Namespace;
    using Store = PairingNvsStorage::Store;

    static constexpr char kNamespace[] = "ot_direct";
    static constexpr char kCatalogKey[] = "catalog";
    static constexpr std::size_t kScopeCount = board_storage_budget::kDirectConversations;
    static constexpr std::size_t kCatalogBytes = board_storage_budget::kDirectCatalogBytes;

    class Scope final : public security_evaluation::EvaluationGenerationBackend,
                        public security_evaluation::PairingCompletionArchive {
    public:
        persistence::PersistentStorage& external(Store);
        persistence::PersistentStorage* archive_storage(std::uint64_t) override;
        std::uint64_t archive_capacity() const override { return PairingNvsStorage::kCapacity; }

        persistence::StorageReadResult read(std::uint64_t, Namespace, Domain, std::size_t,
                                             persistence::MutableStorageByteView) override;
        Error erase(std::uint64_t, Namespace, Domain, std::size_t) override;
        Error write(std::uint64_t, Namespace, Domain, std::size_t, std::size_t,
                    persistence::StorageByteView) override;
        Error sync(std::uint64_t, Namespace, Domain, std::size_t) override;

    private:
        friend class DirectNvsStorage;
        Scope(DirectNvsStorage&, unsigned slot);
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        class View final : public persistence::PersistentStorage {
        public:
            View(Scope& scope, unsigned store, std::uint64_t generation = 0)
                : scope_(scope), store_(store), generation_(generation) {}
            persistence::StorageReadResult read_slot(Domain, std::size_t, persistence::MutableStorageByteView) override;
            Error erase_slot(Domain, std::size_t) override;
            Error write_slot(Domain, std::size_t, std::size_t, persistence::StorageByteView) override;
            Error sync_slot(Domain, std::size_t) override;
        private:
            Scope& scope_;
            const unsigned store_;
            const std::uint64_t generation_;
        };
        DirectNvsStorage& owner_;
        const unsigned slot_;
        View denied_;
        std::array<View, 5> external_;
        std::array<std::optional<View>, PairingNvsStorage::kCapacity> archives_{};
    };

    DirectNvsStorage();
    ~DirectNvsStorage() override;
    DirectNvsStorage(const DirectNvsStorage&) = delete;
    DirectNvsStorage& operator=(const DirectNvsStorage&) = delete;

    [[nodiscard]] bool ready();
    Scope* scope(std::size_t slot) { return slot < scopes_.size() ? &scopes_[slot] : nullptr; }

    // Runs after durable denial and borrower drain. Repeating after a crash or
    // an already-complete deletion is safe. Consent/catalog is owned separately.
    [[nodiscard]] bool clear_scope(std::size_t slot);
    void retire();

    KvError read_blob(const char*, const char*, const char*, std::uint8_t*, std::size_t,
                      std::size_t&) override;
    KvError write_blob(const char*, const char*, const char*, const std::uint8_t*, std::size_t) override;
    KvError erase_key(const char*, const char*, const char*) override;
    KvError commit(const char*, const char*) override;

private:
    struct Coordinate {
        unsigned scope;
        std::uint64_t generation;
        unsigned store;
        Domain domain;
        std::size_t slot;
    };
    persistence::StorageReadResult read_tuple(Coordinate, persistence::MutableStorageByteView);
    Error erase_tuple(Coordinate);
    Error write_tuple(Coordinate, std::size_t, persistence::StorageByteView);
    Error sync_tuple(Coordinate);

    KvError read_record(const char*, bool known, std::uint8_t*, std::size_t);
    bool commit_record(const char*, bool known, const std::uint8_t*, std::size_t, bool erase);
    bool stage_tuple(Coordinate);
    void clear_stage();
    KvError fault();

    nvs_handle_t handle_{};
    bool mounted_{}, retired_{}, writable_{}, good_{}, erase_staged_{};
    std::size_t staged_size_{};
    std::array<char, 16> staged_key_{};
    std::array<std::uint8_t, kCatalogBytes> staged_{};
    std::array<std::uint8_t, kCatalogBytes> verify_{};
    std::array<Scope, kScopeCount> scopes_;
};

}  // namespace opentrail::target::heltec_v4_bench
