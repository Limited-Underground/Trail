#include "fake_gps_provider.hpp"

namespace opentrail::location::test_support {

void FakeGpsProvider::set_fix(const GpsFix& fix) {
    reading_.has_fix = true;
    reading_.fix = fix;
}

void FakeGpsProvider::set_unavailable() {
    reading_ = {};
}

GpsReadResult FakeGpsProvider::latest_fix() const {
    ++read_count_;
    return reading_;
}

std::uint32_t FakeGpsProvider::read_count() const {
    return read_count_;
}

}  // namespace opentrail::location::test_support
