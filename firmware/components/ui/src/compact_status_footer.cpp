#include "opentrail/compact_status_footer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::ui::compact_status_footer {
namespace {

using Glyph = std::array<std::uint8_t, kGlyphWidth>;

constexpr Glyph kBlank{0x00, 0x00, 0x00, 0x00, 0x00};

Glyph glyph(char value) {
    switch (value) {
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case '%': return {0x63, 0x13, 0x08, 0x64, 0x63};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        default: return kBlank;
    }
}

Glyph direction_glyph(Direction direction) {
    if (direction == Direction::tx) {
        return {0x04, 0x02, 0x7F, 0x02, 0x04};
    }
    if (direction == Direction::rx) {
        return {0x10, 0x20, 0x7F, 0x20, 0x10};
    }
    return kBlank;
}

template <std::size_t Size>
void copy_prefix(std::array<char, Size>& destination, const char* text) {
    for (std::size_t index = 0;
         index < destination.size() && text[index] != '\0'; ++index) {
        destination[index] = text[index];
    }
}

template <std::size_t Size>
std::size_t append_decimal(
    std::array<char, Size>& destination,
    std::size_t offset,
    std::uint16_t value) {
    std::array<char, 3> digits{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value = static_cast<std::uint16_t>(value / 10U);
    } while (value != 0U && count < digits.size());
    for (std::size_t index = 0;
         index < count && offset < destination.size(); ++index) {
        destination[offset++] = digits[count - index - 1U];
    }
    return offset;
}

bool fresh_valid_metric(
    const Metric& metric,
    std::uint16_t maximum,
    std::uint64_t fresh_for_ms,
    std::uint64_t now_ms) {
    return metric.state == ObservationState::valid &&
           metric.value <= maximum && fresh_for_ms != 0U &&
           now_ms >= metric.sampled_at_ms &&
           now_ms - metric.sampled_at_ms < fresh_for_ms;
}

template <std::size_t Size>
void render_text(Page& page,
                 std::size_t start_x,
                 const std::array<char, Size>& text) {
    for (std::size_t character = 0; character < text.size(); ++character) {
        const auto columns = glyph(text[character]);
        const auto x = start_x + character * (kGlyphWidth + 1U);
        for (std::size_t column = 0; column < columns.size(); ++column) {
            page.columns[x + column] = columns[column];
        }
    }
}

}  // namespace

Fields format(
    const Snapshot& snapshot,
    Freshness freshness,
    std::uint64_t now_ms) {
    Fields fields{};
    copy_prefix(fields.battery, "BAT:");
    if (fresh_valid_metric(snapshot.battery_percent, 100U,
                           freshness.battery_fresh_for_ms, now_ms)) {
        const auto offset = append_decimal(
            fields.battery, 4U, snapshot.battery_percent.value);
        fields.battery[offset] = '%';
    } else {
        fields.battery[4] = '-';
        fields.battery[5] = '-';
        fields.battery[6] = '%';
    }

    copy_prefix(fields.gps, "GPS:");
    if (fresh_valid_metric(snapshot.gps_satellites, 99U,
                           freshness.gps_fresh_for_ms, now_ms)) {
        append_decimal(fields.gps, 4U, snapshot.gps_satellites.value);
    } else {
        fields.gps[4] = '-';
        fields.gps[5] = '-';
    }

    copy_prefix(fields.ble, "BLE:");
    switch (snapshot.ble) {
        case BleCode::starting: fields.ble[4] = 'S'; break;
        case BleCode::advertising: fields.ble[4] = 'A'; break;
        case BleCode::connected: fields.ble[4] = 'C'; break;
        case BleCode::retrying: fields.ble[4] = 'R'; break;
        case BleCode::error: fields.ble[4] = 'E'; break;
        case BleCode::unavailable:
        default: fields.ble[4] = '-'; break;
    }

    return fields;
}

Page render(
    const Fields& fields,
    const ActivityOwner& activity,
    std::uint64_t now_ms) {
    Page page{};
    render_text(page, 1U, fields.battery);
    render_text(page, 51U, fields.gps);
    render_text(page, 89U, fields.ble);
    const auto arrow = direction_glyph(activity.visible_direction(now_ms));
    for (std::size_t column = 0; column < arrow.size(); ++column) {
        page.columns[121U + column] = arrow[column];
    }
    return page;
}

ActivityOwner::ActivityOwner(
    bool transport_supported,
    std::uint64_t visible_for_ms)
    : transport_supported_(transport_supported),
      visible_for_ms_(visible_for_ms) {}

bool ActivityOwner::observe_accepted_transport_event(
    Direction direction,
    std::uint64_t at_ms) {
    if (!transport_supported_ || visible_for_ms_ == 0U ||
        (direction != Direction::tx && direction != Direction::rx) ||
        (event_valid_ && at_ms < event_at_ms_)) {
        return false;
    }
    event_valid_ = true;
    direction_ = direction;
    event_at_ms_ = at_ms;
    return true;
}

Direction ActivityOwner::visible_direction(std::uint64_t now_ms) const {
    if (!transport_supported_ || visible_for_ms_ == 0U || !event_valid_ ||
        now_ms < event_at_ms_ || now_ms - event_at_ms_ >= visible_for_ms_) {
        return Direction::none;
    }
    return direction_;
}

void ActivityOwner::clear() {
    event_valid_ = false;
    direction_ = Direction::none;
    event_at_ms_ = 0;
}

}  // namespace opentrail::ui::compact_status_footer
