#include "opentrail/otxt_payload_codec.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace otxt = opentrail::companion::otxt;
namespace packet = opentrail::protocol::v1;
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const std::string& detail) {
    if (!condition) {
        std::cerr << "FAIL OTXT " << detail << '\n';
        std::exit(1);
    }
}

packet::View view(const Bytes& bytes) {
    return {bytes.data(), bytes.size()};
}

bool equals(packet::View actual, const Bytes& expected) {
    return actual.data && actual.size == expected.size() &&
        std::equal(expected.begin(), expected.end(), actual.data);
}

Bytes hex_bytes(const std::string& hex) {
    expect(hex.size() % 2 == 0, "fixture hex length");
    Bytes result;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        expect(hex.find_first_not_of("0123456789abcdef", i) >= i + 2, "fixture hex digit");
        result.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

Bytes base64_bytes(const std::string& encoded) {
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::uint32_t bits = 0;
    unsigned pending_bits = 0;
    Bytes result;
    for (const char letter : encoded) {
        if (letter == '=') {
            break;
        }
        const auto value = alphabet.find(letter);
        expect(value != std::string::npos, "fixture Base64 digit");
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        pending_bits += 6;
        if (pending_bits >= 8) {
            pending_bits -= 8;
            result.push_back(static_cast<std::uint8_t>(bits >> pending_bits));
        }
    }
    return result;
}

packet::Context vector_context() {
    packet::Context context{};
    // Only structural Packet admission uses this public synthetic context.
    context.sender.fill(1);
    context.recipient.fill(2);
    context.transcript.fill(3);
    context.sender_alias = 0x1122334455667788ULL;
    context.recipient_alias = 0x8877665544332211ULL;
    context.group = 1;
    context.epoch = 0x01020304;
    return context;
}

void packet_vector(const std::string& id, const std::string& fault,
                   const Bytes& frame, const Bytes& valid_frame) {
    expect(otxt::decode(view(frame)).size == 0, id + " ciphertext kept outside the payload codec");
    const auto context = vector_context();
    const auto parsed = packet::decode_frame(view(frame), context, 158);
    if (fault == "wrong_recipient") {
        expect(parsed.error == packet::Error::invalid_context, id);
    } else if (fault == "unknown_packet_version") {
        expect(parsed.error == packet::Error::unsupported_version, id);
    } else if (fault == "malformed" || fault == "fragmentation_forbidden") {
        expect(parsed.error == packet::Error::malformed, id);
    } else if (fault == "replay_no_redelivery") {
        // The fresh-session integration test, which has test keys, checks the replay store.
        expect(parsed.error == packet::Error::none && frame == valid_frame, id);
    } else if (fault == "authentication_failed") {
        // The fixture carries sealed bytes only. Assert the exact tag fault and keep it opaque.
        expect(parsed.error == packet::Error::none && frame.size() == valid_frame.size() &&
            std::equal(frame.begin(), frame.end() - 1, valid_frame.begin()) &&
            (frame.back() ^ valid_frame.back()) == 1, id);
    } else {
        expect(false, id + " unknown Packet vector");
    }
}

void boundaries() {
    const Bytes valid{0x20, 0, '\r', '\n', 0xf4, 0x8f, 0xbf, 0xbf};
    std::array<std::uint8_t, otxt::envelope_bytes + otxt::max_text_bytes> buffer{};
    buffer.fill(0x7e);
    const auto original = buffer;
    expect(otxt::encode(view(valid), {buffer.data(), valid.size() + 7}) == 0 &&
        buffer == original, "short destination preserves output");
    expect(otxt::encode(view(valid), {buffer.data(), buffer.size()}, 68) == 0 &&
        buffer == original, "no usable Packet space preserves output");
    expect(otxt::encode({}, {buffer.data(), buffer.size()}) == 0 &&
        buffer == original, "empty text preserves output");

    const Bytes bad_utf8{0xed, 0xa0, 0x80};
    expect(otxt::encode(view(bad_utf8), {buffer.data(), buffer.size()}) == 0 &&
        buffer == original, "invalid scalar preserves output");
    const Bytes maximum_small_link(86, 'h');
    expect(otxt::encode(view(maximum_small_link), {buffer.data(), buffer.size()}, 154) == 94,
           "86 bytes fit MTU 154");
    expect(equals(otxt::decode({buffer.data(), 94}, 154), maximum_small_link),
           "86 bytes decode at MTU 154");
    const auto saved = buffer;
    const Bytes excess_small_link(87, 'h');
    expect(otxt::encode(view(excess_small_link), {buffer.data(), buffer.size()}, 154) == 0 &&
        buffer == saved, "87 bytes refused at MTU 154 before output");

    std::copy(valid.begin(), valid.end(), buffer.begin());
    const auto size = otxt::encode({buffer.data(), valid.size()}, {buffer.data(), buffer.size()});
    expect(size == otxt::envelope_bytes + valid.size() &&
        equals(otxt::decode({buffer.data(), size}), valid), "exact overlapping encode and strict UTF-8");
}
}

int main() {
    const auto fixture = std::filesystem::path(__FILE__).parent_path() /
        "../../android/protocol/src/test/resources/otxt-v1-vectors.tsv";
    std::ifstream input(fixture);
    expect(input.good(), "cannot open shared fixture");
    std::string row;
    std::size_t golden = 0;
    std::size_t rejected = 0;
    std::size_t packet_faults = 0;
    std::size_t budget_faults = 0;
    Bytes ascii_frame;
    Bytes maximum_text;
    Bytes maximum_payload;
    Bytes maximum_frame;
    while (std::getline(input, row)) {
        if (!row.empty() && row.back() == '\r') {
            row.pop_back();
        }
        if (row.empty() || row.front() == '#') {
            continue;
        }
        std::istringstream fields(row);
        std::array<std::string, 7> column;
        for (auto& cell : column) {
            expect(static_cast<bool>(std::getline(fields, cell, '\t')), "incomplete shared vector");
        }
        std::string extra;
        expect(!std::getline(fields, extra, '\t'), "excess shared vector columns");
        const auto& [id, layer, outcome, payload_hex, frame_hex, text64, budget] = column;

        if (layer == "payload" && outcome == "accept") {
            const auto payload = hex_bytes(payload_hex);
            const auto text = base64_bytes(text64);
            const auto frame = hex_bytes(frame_hex);
            const auto parsed = packet::decode_frame(view(frame), vector_context(), 158);
            expect(parsed.error == packet::Error::none && parsed.ciphertext.size == payload.size(), id);
            expect(equals(otxt::decode(view(payload)), text), id + " decode bytes");
            std::array<std::uint8_t, otxt::envelope_bytes + otxt::max_text_bytes> output{};
            const auto size = otxt::encode(view(text), {output.data(), output.size()});
            expect(equals({output.data(), size}, payload), id + " encode bytes");
            if (id == "T-0042-golden-ascii") {
                ascii_frame = frame;
            } else if (id == "T-0042-golden-at-limit") {
                maximum_text = text;
                maximum_payload = payload;
                maximum_frame = frame;
            }
            ++golden;
        } else if (layer == "payload") {
            const auto payload = hex_bytes(payload_hex);
            expect(otxt::decode(view(payload)).size == 0, id);
            ++rejected;
        } else if (layer == "packet") {
            packet_vector(id, outcome, hex_bytes(frame_hex), ascii_frame);
            ++packet_faults;
        } else if (layer == "budget" && outcome == "oversize") {
            const auto frame = hex_bytes(frame_hex);
            const auto mtu = std::stoul(budget);
            expect(frame == maximum_frame && frame.size() > mtu &&
                packet::decode_frame(view(frame), vector_context(), mtu).error == packet::Error::bounds,
                id + " Packet MTU");
            std::array<std::uint8_t, otxt::envelope_bytes + otxt::max_text_bytes> output{};
            output.fill(0x7e);
            const auto original = output;
            expect(otxt::decode(view(maximum_payload), mtu).size == 0 &&
                otxt::encode(view(maximum_text), {output.data(), output.size()}, mtu) == 0 &&
                output == original, id + " leaves payload output unchanged");
            ++budget_faults;
        } else {
            expect(false, id + " unknown shared vector");
        }
    }
    expect(golden == 3 && rejected == 14 && packet_faults == 6 && budget_faults == 1,
           "all 24 shared vectors exercised");
    boundaries();
    std::cout << "PASS 24 OTXT shared vectors and codec boundary checks\n";
}
