#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "opentrail/portable_ui_render_plan.hpp"
#include "opentrail/portable_ui_shell.hpp"

namespace {

using namespace opentrail;

constexpr std::size_t kMaximumCommandLength = 4096;
constexpr std::size_t kMaximumFields = 160;
constexpr std::string_view kProtocolVersion = "2";

class HostDisplay final : public ui::DisplaySink {
public:
    ui::DisplayWriteError present(const ui::UiFrame& frame) override {
        frame_ = frame;
        return ui::DisplayWriteError::none;
    }

private:
    ui::UiFrame frame_{};
};

class HostInput final : public ui::LocalInputSource {
public:
    bool set(const ui::LocalInputEvent& event) {
        if (ready_) {
            return false;
        }
        event_ = event;
        ready_ = true;
        return true;
    }

    ui::LocalInputEvent read() override {
        if (!ready_) {
            return {};
        }
        ready_ = false;
        return event_;
    }

    void clear() {
        event_ = {};
        ready_ = false;
    }

private:
    ui::LocalInputEvent event_{};
    bool ready_{false};
};

std::vector<std::string_view> split(const std::string& line) {
    std::vector<std::string_view> fields{};
    fields.reserve(16);
    std::size_t start = 0;
    while (start <= line.size() && fields.size() <= kMaximumFields) {
        const auto end = line.find('|', start);
        fields.emplace_back(line.data() + start,
                            (end == std::string::npos ? line.size() : end) - start);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return fields;
}

template <typename T>
bool number(std::string_view field, T& output) {
    if (field.empty()) {
        return false;
    }
    T value{};
    const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size()) {
        return false;
    }
    output = value;
    return true;
}

bool boolean(std::string_view field, bool& output) {
    std::uint8_t value = 0;
    if (!number(field, value) || value > 1) {
        return false;
    }
    output = value == 1;
    return true;
}

bool uppercase_hex(std::string_view field,
                   std::uint8_t byte_count,
                   ui::UiOwnedText& output,
                   bool truncated) {
    if (byte_count == 0) {
        if (field != "-") {
            return false;
        }
        output = {};
        output.truncated = truncated;
        return true;
    }
    if (field.size() != byte_count * 2U) {
        return false;
    }
    output = {};
    output.byte_count = byte_count;
    output.truncated = truncated;
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < byte_count; ++index) {
        const auto high = nibble(field[index * 2U]);
        const auto low = nibble(field[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        const auto byte = static_cast<unsigned char>((high << 4) | low);
        if (byte < 0x20U || byte > 0x7EU) return false;
        output.bytes[index] = static_cast<char>(byte);
    }
    return true;
}

std::string hex_text(const ui::UiOwnedText& text) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string result{};
    result.reserve(text.byte_count * 2U);
    for (std::size_t index = 0; index < text.byte_count; ++index) {
        const auto byte = static_cast<unsigned char>(text.bytes[index]);
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

bool snapshot(const std::vector<std::string_view>& fields,
              std::size_t start,
              integration::PortableUiSnapshot& output) {
    if (fields.size() < start + 14) {
        return false;
    }
    std::uint8_t position = 0;
    std::uint8_t archive = 0;
    std::uint8_t radio = 0;
    std::uint8_t location = 0;
    std::uint8_t power = 0;
    bool peer_valid = false;
    bool archive_valid = false;
    bool recovery_valid = false;
    if (!number(fields[start], position) || position > 4 ||
        !number(fields[start + 1], archive) || archive > 7 ||
        !number(fields[start + 2], radio) || radio > 4 ||
        !number(fields[start + 3], location) || location > 4 ||
        !number(fields[start + 4], power) || power > 4 ||
        !boolean(fields[start + 5], peer_valid) ||
        !number(fields[start + 6], output.status.peer_count) ||
        !number(fields[start + 7], output.status.unread_messages) ||
        !boolean(fields[start + 8], archive_valid) ||
        !number(fields[start + 9], output.status.archive_queue_count) ||
        !boolean(fields[start + 10], recovery_valid) ||
        !number(fields[start + 11], output.recovery_diagnostic_word) ||
        !number(fields[start + 12], output.bridge_session_epoch) ||
        !number(fields[start + 13], output.message_count) ||
        output.message_count > output.messages.size() ||
        fields.size() != start + 14U + output.message_count * 10U) {
        return false;
    }
    output.position = static_cast<integration::PortableUiPositionState>(position);
    output.archive = static_cast<integration::PortableUiArchiveState>(archive);
    output.status.radio = static_cast<ui::UiIndicatorState>(radio);
    output.status.position = static_cast<ui::UiIndicatorState>(location);
    output.status.power = static_cast<ui::UiIndicatorState>(power);
    output.status.peer_count_valid = peer_valid;
    output.status.archive_queue_count_valid = archive_valid;
    output.recovery_diagnostic_valid = recovery_valid;
    std::size_t index = start + 14U;
    for (std::size_t message_index = 0;
         message_index < output.message_count; ++message_index) {
        auto& message = output.messages[message_index];
        std::uint8_t direction = 0;
        std::uint8_t kind = 0;
        std::uint8_t priority = 0;
        std::uint8_t delivery = 0;
        bool acknowledge = false;
        bool truncated = false;
        bool text_unavailable = false;
        std::uint8_t text_bytes = 0;
        if (!number(fields[index++], message.sequence) ||
            !number(fields[index++], direction) || direction > 1 ||
            !number(fields[index++], kind) || kind > 3 ||
            !number(fields[index++], priority) || priority > 2 ||
            !number(fields[index++], delivery) || delivery > 4 ||
            !boolean(fields[index++], acknowledge) ||
            !boolean(fields[index++], truncated) ||
            !boolean(fields[index++], text_unavailable) ||
            !number(fields[index++], text_bytes) ||
            text_bytes > ui::kMaxUiOwnedTextBytes ||
            !uppercase_hex(fields[index++], text_bytes, message.text,
                           truncated)) {
            return false;
        }
        message.direction =
            static_cast<integration::PortableUiMessageDirection>(direction);
        message.kind = static_cast<integration::PortableUiMessageKind>(kind);
        message.priority =
            static_cast<integration::PortableUiMessagePriority>(priority);
        message.delivery =
            static_cast<integration::PortableUiMessageDeliveryState>(delivery);
        message.text_unavailable = text_unavailable;
        message.acknowledge_available = acknowledge;
    }
    return true;
}

template <typename Enum>
unsigned value(Enum input) {
    return static_cast<unsigned>(input);
}

std::string offer_line(
    const integration::PortableUiShellResult& result,
    const ui::UiFrame& frame,
    const ui::UiPresentationSidecar& sidecar,
    const ui::UiRenderPlan& plan) {
    std::ostringstream line;
    line << "OFFER|2|" << result.generation << '|' << result.revision << '|'
         << value(frame.screen) << '|' << value(frame.attention) << '|'
         << value(frame.notice) << '|' << value(frame.status.radio) << '|'
         << value(frame.status.position) << '|' << value(frame.status.power) << '|'
         << (frame.status.peer_count_valid ? 1 : 0) << '|'
         << value(frame.status.peer_count) << '|'
         << value(frame.status.unread_messages) << '|'
         << (frame.status.archive_queue_count_valid ? 1 : 0) << '|'
         << value(frame.status.archive_queue_count) << '|'
         << value(sidecar.owned_text_count);
    for (std::size_t index = 0; index < sidecar.owned_text_count; ++index) {
        const auto& text = sidecar.owned_texts[index];
        line << '|' << (text.truncated ? 1 : 0) << '|'
             << (text.unavailable ? 1 : 0) << '|'
             << value(text.byte_count) << '|' << hex_text(text);
    }
    line << '|' << value(sidecar.messages.list_kind) << '|'
         << value(sidecar.messages.page_index) << '|'
         << value(sidecar.messages.page_count) << '|'
         << value(sidecar.messages.row_count);
    for (std::size_t index = 0; index < sidecar.messages.row_count; ++index) {
        const auto& row = sidecar.messages.rows[index];
        line << '|' << value(row.text_index) << '|'
             << value(row.delivery) << '|' << value(row.kind) << '|'
             << value(row.priority) << '|' << (row.unread ? 1 : 0);
    }
    line << '|' << (sidecar.messages.detail_valid ? 1 : 0) << '|'
         << value(sidecar.messages.detail_delivery) << '|'
         << value(sidecar.messages.detail_kind) << '|'
         << value(sidecar.messages.detail_priority) << '|'
         << (sidecar.messages.detail_unread ? 1 : 0) << '|'
         << (sidecar.messages.detail_acknowledge_available ? 1 : 0) << '|'
         << value(sidecar.messages.compose_template_id) << '|'
         << value(frame.action_count);
    for (std::size_t index = 0; index < frame.action_count; ++index) {
        line << '|' << value(frame.actions[index].action) << '|'
             << (frame.actions[index].enabled ? 1 : 0);
    }
    line << '|' << plan.width << '|' << plan.height << '|'
         << value(plan.shape) << '|'
         << value(plan.primitive_count);
    for (std::size_t index = 0; index < plan.primitive_count; ++index) {
        const auto& primitive = plan.primitives[index];
        line << '|' << value(primitive.kind) << '|' << primitive.bounds.x << '|'
             << primitive.bounds.y << '|' << primitive.bounds.width << '|'
             << primitive.bounds.height << '|' << value(primitive.text) << '|'
             << value(primitive.style) << '|' << value(primitive.action_slot) << '|'
             << (primitive.enabled ? 1 : 0) << '|'
             << (primitive.requires_hold ? 1 : 0) << '|'
             << (primitive.numeric_value_valid ? 1 : 0) << '|'
             << primitive.numeric_value << '|'
             << value(primitive.owned_text_index);
    }
    return line.str();
}

std::string result_line(const integration::PortableUiShellResult& result) {
    if (result.has_offer) {
        return {};
    }
    if (result.error != integration::PortableUiShellError::none ||
        result.disposition == integration::PortableUiShellDisposition::input_rejected) {
        std::ostringstream line;
        line << "REJECT|2|" << value(result.error) << '|'
             << value(result.action_error) << '|' << value(result.present_error);
        return line.str();
    }
    std::ostringstream line;
    if (result.disposition == integration::PortableUiShellDisposition::idle) {
        line << "IDLE|2|" << result.generation << '|' << result.revision;
    } else {
        const auto request_text =
            result.request_template_id == 0
                ? ui::UiOwnedText{}
                : integration::portable_ui_message_template(
                      result.request_template_id);
        line << "COMMITTED|2|" << result.generation << '|' << result.revision
             << '|' << value(result.disposition) << '|' << result.request_id
             << '|' << value(result.request_kind) << '|'
             << result.request_bridge_session_epoch << '|'
             << value(result.request_template_id) << '|'
             << result.request_message_sequence << '|'
             << value(request_text.byte_count) << '|'
             << (request_text.byte_count == 0 ? "-" : hex_text(request_text));
    }
    return line.str();
}

class HostSession {
public:
    HostSession()
        : local_(display_, input_, {466, 466, 16, 4, true, false, true}),
          shell_(local_) {}

    std::string handle(const std::string& command) {
        if (command.size() > kMaximumCommandLength) {
            return "REJECT|2|COMMAND_TOO_LONG";
        }
        const auto fields = split(command);
        if (fields.size() < 2 || fields[1] != kProtocolVersion) {
            return "REJECT|2|VERSION";
        }
        integration::PortableUiShellResult result{};
        if (fields[0] == "START") {
            integration::PortableUiSnapshot state{};
            if (!snapshot(fields, 2, state)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.prepare_activate(state);
        } else if (fields[0] == "INPUT") {
            if (fields.size() != 6) {
                return "REJECT|2|SCHEMA";
            }
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            std::uint8_t slot = 0;
            if (!number(fields[2], generation) || !number(fields[3], revision) ||
                !number(fields[4], slot) || slot > 3 ||
                (fields[5] != "A" && fields[5] != "H")) {
                return "REJECT|2|SCHEMA";
            }
            const auto state = shell_.status();
            if (generation != state.generation || revision != state.active_revision ||
                !input_.set({ui::InputReadError::none, revision, slot,
                             fields[5] == "H" ? ui::InputGesture::hold
                                                : ui::InputGesture::activate})) {
                return "REJECT|2|PRECONDITION";
            }
            result = shell_.prepare_input();
            input_.clear();
        } else if (fields[0] == "REFRESH") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            integration::PortableUiSnapshot state{};
            if (fields.size() < 4 || !number(fields[2], generation) ||
                !number(fields[3], revision) || !snapshot(fields, 4, state)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.prepare_refresh(generation, revision, state);
        } else if (fields[0] == "COMPLETE") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            std::uint32_t request_id = 0;
            std::uint8_t request_kind = 0;
            bool succeeded = false;
            std::uint64_t applied_epoch = 0;
            std::uint64_t applied_message_sequence = 0;
            std::uint8_t request_template_id = 0;
            std::uint64_t request_message_sequence = 0;
            integration::PortableUiSnapshot state{};
            if (fields.size() < 12 || !number(fields[2], generation) ||
                !number(fields[3], revision) || !number(fields[4], request_id) ||
                !number(fields[5], request_kind) || request_kind == 0 ||
                request_kind > 11 || !boolean(fields[6], succeeded) ||
                !number(fields[7], applied_epoch) ||
                !number(fields[8], applied_message_sequence) ||
                !number(fields[9], request_template_id) ||
                request_template_id >
                    integration::kPortableUiMessageTemplateCount ||
                !number(fields[10], request_message_sequence) ||
                !snapshot(fields, 11, state)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.prepare_completion(
                generation, revision, request_id,
                static_cast<integration::PortableUiRequestKind>(request_kind),
                succeeded, applied_epoch, applied_message_sequence,
                request_template_id,
                request_message_sequence,
                state);
        } else if (fields[0] == "PRESENTED") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            if (fields.size() != 4 || !number(fields[2], generation) ||
                !number(fields[3], revision)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.commit_present(generation, revision);
        } else if (fields[0] == "NOT_READY") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            const auto pending = shell_.pending_frame();
            const auto sidecar = shell_.presentation_sidecar();
            const auto state = shell_.status();
            if (fields.size() != 4 || !number(fields[2], generation) ||
                !number(fields[3], revision) || !state.has_pending_offer ||
                generation != pending_generation_ || revision != pending.revision ||
                revision != pending_revision_) {
                return "REJECT|2|PRECONDITION";
            }
            const auto plan = ui::make_portable_ui_render_plan(
                pending, sidecar, ui::kSimulatorLogicalDisplayProfile);
            if (!plan.ok()) {
                return "REJECT|2|PLAN";
            }
            integration::PortableUiShellResult retry{};
            retry.disposition = integration::PortableUiShellDisposition::offer_ready;
            retry.generation = generation;
            retry.revision = revision;
            retry.has_offer = true;
            return offer_line(retry, pending, sidecar, plan.plan);
        } else if (fields[0] == "RENDER_FAILED") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            if (fields.size() != 4 || !number(fields[2], generation) ||
                !number(fields[3], revision)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.reject_present(generation, revision);
        } else if (fields[0] == "CLOSE") {
            std::uint32_t generation = 0;
            std::uint32_t revision = 0;
            if (fields.size() != 4 || !number(fields[2], generation) ||
                !number(fields[3], revision)) {
                return "REJECT|2|SCHEMA";
            }
            result = shell_.close_session(generation, revision);
        } else if (fields[0] == "QUIT" && fields.size() == 2) {
            closed_ = true;
            return "BYE|2";
        } else {
            return "REJECT|2|COMMAND";
        }

        if (result.has_offer) {
            const auto frame = shell_.pending_frame();
            const auto sidecar = shell_.presentation_sidecar();
            const auto plan = ui::make_portable_ui_render_plan(
                frame, sidecar, ui::kSimulatorLogicalDisplayProfile);
            if (!plan.ok()) {
                const auto ignored = shell_.reject_present(
                    result.generation, result.revision);
                static_cast<void>(ignored);
                return "REJECT|2|PLAN";
            }
            pending_generation_ = result.generation;
            pending_revision_ = result.revision;
            return offer_line(result, frame, sidecar, plan.plan);
        }
        if (!shell_.status().has_pending_offer) {
            pending_generation_ = 0;
            pending_revision_ = 0;
        }
        return result_line(result);
    }

    bool closed() const { return closed_; }

private:
    HostDisplay display_{};
    HostInput input_{};
    ui::CheckedLocalInterface local_;
    integration::PortableUiShell shell_;
    bool closed_{false};
    std::uint32_t pending_generation_{0};
    std::uint32_t pending_revision_{0};
};

}  // namespace

int main() {
    HostSession session{};
    std::array<char, kMaximumCommandLength + 2> buffer{};
    while (true) {
        std::cin.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (std::cin.bad()) {
            return 1;
        }
        if (std::cin.fail() && !std::cin.eof()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "REJECT|2|COMMAND_TOO_LONG\n" << std::flush;
            continue;
        }
        if (std::cin.eof() && std::cin.gcount() == 0) {
            return 0;
        }
        if (std::cin.eof()) {
            std::cout << "REJECT|2|INCOMPLETE\n" << std::flush;
            return 0;
        }
        const auto extracted = static_cast<std::size_t>(std::cin.gcount());
        const auto length = extracted == 0 ? 0 : extracted - 1U;
        const std::string line(buffer.data(), length);
        if (std::any_of(line.begin(), line.end(), [](char character) {
                const auto byte = static_cast<unsigned char>(character);
                return byte < 0x20U || byte > 0x7EU;
            })) {
            std::cout << "REJECT|2|SCHEMA\n" << std::flush;
            continue;
        }
        const auto response = session.handle(line);
        std::cout << response << '\n' << std::flush;
        if (session.closed()) {
            return 0;
        }
        if (std::cin.eof()) {
            return 0;
        }
    }
}
