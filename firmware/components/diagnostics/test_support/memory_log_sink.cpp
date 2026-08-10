#include "memory_log_sink.hpp"

namespace opentrail::diagnostics::test_support {

bool MemoryLogSink::write(const LogRecord& record) {
    if (count_ == kCapacity) {
        ++rejected_;
        return false;
    }
    records_[count_++] = record;
    return true;
}

bool MemoryLogSink::empty() const {
    return count_ == 0;
}

std::size_t MemoryLogSink::size() const {
    return count_;
}

std::uint32_t MemoryLogSink::rejected() const {
    return rejected_;
}

const LogRecord* MemoryLogSink::at(std::size_t index) const {
    return index < count_ ? &records_[index] : nullptr;
}

void MemoryLogSink::clear() {
    records_ = {};
    count_ = 0;
    rejected_ = 0;
}

}  // namespace opentrail::diagnostics::test_support
