#include "fake_local_interface.hpp"

#include <limits>

namespace opentrail::ui::test_support {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

bool FakeDisplaySink::enqueue_result(DisplayWriteError error) {
    if (size_ == results_.size()) {
        return false;
    }
    const auto tail = (head_ + size_) % results_.size();
    results_[tail] = error;
    ++size_;
    return true;
}

DisplayWriteError FakeDisplaySink::present(const UiFrame& frame) {
    saturating_increment(present_count_);
    DisplayWriteError result = DisplayWriteError::none;
    if (size_ != 0) {
        result = results_[head_];
        head_ = (head_ + 1) % results_.size();
        --size_;
    }
    if (result == DisplayWriteError::none) {
        latest_frame_ = frame;
        has_frame_ = true;
        saturating_increment(success_count_);
    }
    return result;
}

bool FakeDisplaySink::has_presented_frame() const {
    return has_frame_;
}

UiFrame FakeDisplaySink::latest_frame() const {
    return latest_frame_;
}

std::size_t FakeDisplaySink::queued_result_count() const {
    return size_;
}

std::uint32_t FakeDisplaySink::present_count() const {
    return present_count_;
}

std::uint32_t FakeDisplaySink::success_count() const {
    return success_count_;
}

bool FakeLocalInputSource::enqueue(const LocalInputEvent& event) {
    if (size_ == events_.size()) {
        return false;
    }
    const auto tail = (head_ + size_) % events_.size();
    events_[tail] = event;
    ++size_;
    return true;
}

bool FakeLocalInputSource::enqueue_action(std::uint32_t frame_revision,
                                          std::uint8_t action_slot,
                                          InputGesture gesture) {
    return enqueue({InputReadError::none, frame_revision, action_slot, gesture});
}

bool FakeLocalInputSource::enqueue_not_ready() {
    return enqueue({});
}

bool FakeLocalInputSource::enqueue_failure() {
    LocalInputEvent event{};
    event.error = InputReadError::source_failed;
    return enqueue(event);
}

LocalInputEvent FakeLocalInputSource::read() {
    saturating_increment(read_count_);
    if (size_ == 0) {
        return {};
    }
    const auto event = events_[head_];
    head_ = (head_ + 1) % events_.size();
    --size_;
    return event;
}

std::size_t FakeLocalInputSource::queued_count() const {
    return size_;
}

std::uint32_t FakeLocalInputSource::read_count() const {
    return read_count_;
}

}  // namespace opentrail::ui::test_support
