#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/logger.hpp"

namespace opentrail::diagnostics::test_support {

class MemoryLogSink final : public LogSink {
public:
    static constexpr std::size_t kCapacity = 16;

    bool write(const LogRecord& record) override;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint32_t rejected() const;
    [[nodiscard]] const LogRecord* at(std::size_t index) const;
    void clear();

private:
    std::array<LogRecord, kCapacity> records_{};
    std::size_t count_{0};
    std::uint32_t rejected_{0};
};

}  // namespace opentrail::diagnostics::test_support
