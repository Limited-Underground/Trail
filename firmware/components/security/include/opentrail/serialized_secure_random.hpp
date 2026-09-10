#pragma once
#include <atomic>
#include "opentrail/secure_random.hpp"

namespace opentrail::security {
// Additive, currently unwired lifecycle guard. The owner must exclusively route
// all underlying fills through this object. Probe/source calls must be bounded,
// nonthrowing and must not recursively enter the guard. Probe attests the actual
// source/configuration, not merely a caller's desired readiness flag.
//
// One lifecycle owner calls activate/revoke. Enable the physical source before
// activate. BEFORE disabling it, call revoke: false means a fill is still in
// flight. Keep the physical source enabled and retry under the owner's bounded
// shutdown policy until revoke returns true. Never disable on timeout/failure.
// Do not reactivate until that shutdown/startup operation has completed. Mutate
// underlying adapter state only while revoked and quiescent. Not an ISR API.
// The source and probe context must outlive this object and every in-flight call.
// state() is an advisory snapshot; fill always independently checks admission.
class SerializedSecureRandomSource final : public SecureRandomSource {
public:
    using ReadinessProbe = bool (*)(void*) noexcept;
    SerializedSecureRandomSource(SecureRandomSource& source,
                                 ReadinessProbe probe, void* context)
        : source_(source), probe_(probe), context_(context) {}
    [[nodiscard]] bool activate();
    [[nodiscard]] bool revoke();
    [[nodiscard]] EntropyState state() const override;
    [[nodiscard]] RandomFillResult fill(std::uint8_t*, std::size_t) override;
private:
    bool qualified() const;
    SecureRandomSource& source_;
    ReadinessProbe probe_;
    void* context_;
    std::atomic<bool> admitted_{false};
    mutable std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
};
} // namespace opentrail::security
