#include "opentrail/oled_presentation.hpp"

#include <algorithm>
#include <cstdio>

namespace opentrail::ui::oled_presentation {
namespace {
constexpr std::size_t kGlyphWidth = 5;
constexpr std::size_t kGlyphAdvance = 6;
// Digit/letter glyphs copied from the existing Heltec target renderer;
// punctuation extends that same five-column, seven-bit font for this host model.
std::array<std::uint8_t, kGlyphWidth> glyph_for(char value) {
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
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x7F, 0x20, 0x18, 0x20, 0x7F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x03, 0x04, 0x78, 0x04, 0x03};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
        case ' ': return {0, 0, 0, 0, 0};
        case ':': return {0, 0x36, 0x36, 0, 0};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '~': return {0x08, 0x04, 0x08, 0x10, 0x08};
        case '%': return {0x63, 0x13, 0x08, 0x64, 0x63};
        case '.': return {0, 0x60, 0x60, 0, 0};
        case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
        case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
        default: return {0, 0, 0, 0, 0};
    }
}

char sanitized(char value) {
    if (value >= 'a' && value <= 'z') value = static_cast<char>(value - 'a' + 'A');
    if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')) return value;
    switch (value) {
        case ' ': case '?': case ':': case '-': case '~': case '%': case '.': case '/': case '_':
            return value;
        default: return '?';
    }
}

void row(Frame& frame, std::size_t index, std::string_view value) {
    const auto length = std::min(value.size(), kCharacters);
    for (std::size_t i = 0; i < length; ++i) frame.rows[index][i] = sanitized(value[i]);
    if (value.size() > kCharacters) {
        frame.rows[index][kCharacters - 1] = '~';
        frame.clipped = true;
    }
}

bool fresh(std::uint64_t sampled, std::uint64_t now) {
    return sampled <= now && now - sampled < kMetricFreshnessMs;
}

bool metric(const Metric& value, std::uint16_t maximum, std::uint64_t now) {
    return value.valid && value.value <= maximum && fresh(value.sampled_at_ms, now);
}

bool pulse(const Pulse& value, std::uint64_t now) {
    return value.valid && value.at_ms <= now && now - value.at_ms < kActivityPulseMs;
}

bool pairing(const PairingWindow& value, std::uint64_t now) {
    return value.active && value.deadline_ms > value.start_ms &&
        value.deadline_ms - value.start_ms <= kPairingWindowMaxMs &&
        now >= value.start_ms && now < value.deadline_ms &&
        std::all_of(value.digits.begin(), value.digits.end(), [](char digit) {
            return digit >= '0' && digit <= '9';
        });
}

const char* phone(PhoneState value) {
    switch (value) {
        case PhoneState::ready: return "PHONE READY";
        case PhoneState::reconnecting: return "PHONE RECONNECTING";
        case PhoneState::disconnected: return "PHONE DISCONNECTED";
        default: return "PHONE UNKNOWN";
    }
}

const char* group(GroupState value) {
    switch (value) {
        case GroupState::none: return "NONE";
        case GroupState::member: return "MEMBER";
        case GroupState::administrator: return "ADMIN";
        default: return "?";
    }
}

std::array<char, 9> clock_text(const opentrail::time::OledClockReading& value) {
    constexpr std::array<char, 9> unknown{'-', '-', ':', '-', '-', '\0'};
    if (!value.valid || value.local_second_of_day >= 86'400) return unknown;
    const auto hour = value.local_second_of_day / 3'600;
    const auto minute = value.local_second_of_day / 60 % 60;
    std::array<char, 9> expected{};
    switch (value.format) {
        case opentrail::time::OledClockFormat::hour_24:
            std::snprintf(expected.data(), expected.size(), "%02u:%02u",
                static_cast<unsigned>(hour), static_cast<unsigned>(minute));
            break;
        case opentrail::time::OledClockFormat::hour_12:
            std::snprintf(expected.data(), expected.size(), "%02u:%02u %s",
                static_cast<unsigned>(hour % 12 == 0 ? 12 : hour % 12),
                static_cast<unsigned>(minute), hour < 12 ? "AM" : "PM");
            break;
        default: return unknown;
    }
    return value.text == expected ? expected : unknown;
}

void pixels(Frame& frame) {
    for (std::size_t page = 0; page < kRows; ++page) {
        for (std::size_t cell = 0; cell < kCharacters; ++cell) {
            const char value = frame.rows[page][cell];
            if (value == '\0') break;
            const auto glyph = glyph_for(value);
            for (std::size_t column = 0; column < kGlyphWidth; ++column) {
                frame.pixels[page * kWidth + cell * kGlyphAdvance + column] = glyph[column];
            }
        }
    }
}

}  // namespace

Frame render(const Snapshot& snapshot, std::uint64_t now_ms) {
    Frame frame{};
    if (snapshot.failure) {
        frame.surface = Surface::failure;
        row(frame, 0, "SELF CHECK FAIL");
    } else if (snapshot.reset_in_progress) {
        frame.surface = Surface::reset_progress;
        row(frame, 0, "RESETTING");
        row(frame, 1, "DO NOT REMOVE POWER");
    } else if (snapshot.reset_confirmation) {
        frame.surface = Surface::reset_confirmation;
        row(frame, 0, "ERASE ALL TRAIL DATA?");
        row(frame, 1, "RELEASE THEN CONFIRM");
    } else if (pairing(snapshot.pairing, now_ms)) {
        frame.surface = Surface::pairing;
        row(frame, 0, "PAIR");
        row(frame, 1, std::string_view(snapshot.pairing.digits.data(), snapshot.pairing.digits.size()));
        row(frame, 2, snapshot.pairing.setup_label);
    } else if (!snapshot.region_configured) {
        frame.surface = Surface::region_required;
        row(frame, 0, "REGION REQUIRED");
        row(frame, 1, "RADIO TX DISABLED");
    } else {
        frame.surface = Surface::normal;
        row(frame, 0, snapshot.device_name.empty() ? std::string_view("DEVICE") : snapshot.device_name);
        row(frame, 1, phone(snapshot.phone));
        std::array<char, 32> line{};
        std::snprintf(line.data(), line.size(), "GROUP %s LOC %s", group(snapshot.group),
            snapshot.location_available ? (snapshot.location_on ? "ON" : "OFF") : "?");
        row(frame, 2, line.data());

        // Reserve width for TX status even when the non-secret region label is long.
        std::array<char, 7> region{};
        if (snapshot.region_label.empty()) region[0] = '?';
        const auto region_length = std::min(snapshot.region_label.size(), region.size() - 1);
        for (std::size_t i = 0; i < region_length; ++i) region[i] = sanitized(snapshot.region_label[i]);
        if (snapshot.region_label.size() > region.size() - 1) {
            region[region.size() - 2] = '~';
            frame.clipped = true;
        }
        std::snprintf(line.data(), line.size(), "REGION %s TX %s", region.data(), snapshot.tx_available ? "ON" : "OFF");
        row(frame, 3, line.data());

        std::array<char, 4> battery{'-', '-', '\0'};
        std::array<char, 3> satellites{'-', '-', '\0'};
        if (metric(snapshot.battery, 100, now_ms))
            std::snprintf(battery.data(), battery.size(), "%u", static_cast<unsigned>(snapshot.battery.value));
        if (metric(snapshot.satellites, 99, now_ms))
            std::snprintf(satellites.data(), satellites.size(), "%u", static_cast<unsigned>(snapshot.satellites.value));
        std::snprintf(line.data(), line.size(), "BAT:%s%% GPS:%s", battery.data(), satellites.data());
        row(frame, 4, line.data());
        const char* gps = "GPS UNKNOWN";
        if (fresh(snapshot.gps_sampled_at_ms, now_ms)) {
            if (snapshot.gps_fix == GpsFix::fix) gps = "GPS FIX";
            else if (snapshot.gps_fix == GpsFix::no_fix) gps = "GPS NO FIX";
        }
        row(frame, 5, gps);
        const bool tx = pulse(snapshot.tx, now_ms);
        const bool rx = pulse(snapshot.rx, now_ms);
        row(frame, 6, tx ? (rx ? "RADIO TX RX" : "RADIO TX") : (rx ? "RADIO RX" : "RADIO --"));
        const auto clock = clock_text(snapshot.clock);
        std::snprintf(line.data(), line.size(), "TIME %s", clock.data());
        row(frame, 7, line.data());
    }
    pixels(frame);
    return frame;
}

Frame PresentationOwner::present(const Snapshot& snapshot, std::uint64_t now_ms) {
    if (has_observed_ && now_ms < last_ms_) contained_ = true;
    has_observed_ = true;
    last_ms_ = now_ms;
    if (contained_) {
        Snapshot failure{};
        failure.failure = true;
        return render(failure, now_ms);
    }
    return render(snapshot, now_ms);
}

}  // namespace opentrail::ui::oled_presentation
