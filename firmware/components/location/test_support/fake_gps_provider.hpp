#pragma once

#include <cstdint>

#include "opentrail/gps_provider.hpp"

namespace opentrail::location::test_support {

class FakeGpsProvider final : public GpsProvider {
public:
    void set_fix(const GpsFix& fix);
    void set_unavailable();

    [[nodiscard]] GpsReadResult latest_fix() const override;
    [[nodiscard]] std::uint32_t read_count() const;

private:
    GpsReadResult reading_{};
    mutable std::uint32_t read_count_{0};
};

}  // namespace opentrail::location::test_support
