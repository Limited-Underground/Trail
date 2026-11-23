// OTDC v1 direct conversation configuration codec (F9/A6), against the shared
// vector file the Android protocol tests also read.
#include "opentrail/companion_direct_configuration.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace opentrail::companion;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", what.c_str()); }
}

std::vector<std::uint8_t> hex(const std::string& text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}

struct Row { std::string name, type; bool accepted; std::vector<std::uint8_t> bytes; };

std::vector<Row> load(const char* path) {
    std::ifstream in(path);
    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        Row row;
        std::string accepted, bytes;
        std::getline(fields, row.name, '\t');
        std::getline(fields, row.type, '\t');
        std::getline(fields, accepted, '\t');
        std::getline(fields, bytes, '\t');
        row.accepted = accepted == "1";
        row.bytes = hex(bytes);
        rows.push_back(row);
    }
    return rows;
}

template<class Decode, class Encode>
void round_trip(const Row& row, Decode decode, Encode encode) {
    const auto decoded = decode(row.bytes.data(), row.bytes.size());
    check(decoded.decoded() == row.accepted, row.name + " acceptance");
    if (!decoded.decoded()) return;
    std::array<std::uint8_t, kConfigurationPayloadBytes> out{};
    const auto encoded = encode(decoded.value, out.data(), out.size());
    check(encoded.encoded() && encoded.encoded_bytes == row.bytes.size() &&
          std::equal(row.bytes.begin(), row.bytes.end(), out.begin()), row.name + " round trip");
    // A short output buffer leaves the output untouched.
    std::array<std::uint8_t, kConfigurationPayloadBytes> small{};
    const auto refused = encode(decoded.value, small.data(), row.bytes.size() - 1);
    check(refused.error == ConfigurationCodecError::output_too_small, row.name + " short output");
    // Header damage is always refused.
    for (std::size_t i = 0; i < kDirectConfigurationHeaderBytes; ++i) {
        if (i == 5 || i == 6) continue;  // operation and status are covered by named rows
        auto damaged = row.bytes;
        damaged[i] ^= 0x20;
        check(!decode(damaged.data(), damaged.size()).decoded(), row.name + " header byte " + std::to_string(i));
    }
}

void fields() {
    DirectConfigurationCommand send{DirectConfigurationOperation::send};
    send.text.slot = 3;
    send.text.generation = 0x0807060504030201ULL;
    send.text.message_id = 0x0a0b0c0dU;
    const std::string body = "Meet at the ridge";
    send.text.size = static_cast<std::uint8_t>(body.size());
    std::copy(body.begin(), body.end(), send.text.bytes.begin());
    std::array<std::uint8_t, kConfigurationPayloadBytes> out{};
    const auto encoded = encode_direct_configuration_command(send, out.data(), out.size());
    check(encoded.encoded() && encoded.encoded_bytes == 24 + body.size(), "send size");
    const auto back = decode_direct_configuration_command(out.data(), encoded.encoded_bytes);
    check(back.decoded() && back.value.text.slot == 3 && back.value.text.message_id == 0x0a0b0c0dU &&
          back.value.text.generation == 0x0807060504030201ULL, "send fields");

    // Commands the board alone emits are never accepted from the app.
    DirectConfigurationCommand event{DirectConfigurationOperation::received};
    event.text = send.text;
    check(!encode_direct_configuration_command(event, out.data(), out.size()).encoded(), "received command refused");

    // Unblock carries the peer only.
    DirectConfigurationCommand unblock{DirectConfigurationOperation::unblock};
    unblock.binding.peer[0] = 1;
    unblock.binding.scope[0] = 1;
    check(!encode_direct_configuration_command(unblock, out.data(), out.size()).encoded(), "unblock with scope refused");

    // A conversation that is not Ready never exposes a traffic generation.
    DirectConfigurationResult read{DirectConfigurationOperation::read, DirectConfigurationStatus::ok};
    read.state = DirectConfigurationState::accepted;
    read.binding.peer[0] = 1;
    read.binding.scope[0] = 2;
    read.generation = 9;
    check(!encode_direct_configuration_result(read, out.data(), out.size()).encoded(), "accepted generation refused");
    read.state = DirectConfigurationState::ready;
    check(encode_direct_configuration_result(read, out.data(), out.size()).encoded_bytes == 84, "ready read size");

    check(!decode_direct_configuration_command(nullptr, 8).decoded(), "null input");
    std::vector<std::uint8_t> long_payload(kConfigurationPayloadBytes + 1, 0);
    check(!decode_direct_configuration_result(long_payload.data(), long_payload.size()).decoded(), "oversize");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s vectors.tsv\n", argv[0]); return 2; }
    const auto rows = load(argv[1]);
    check(rows.size() >= 50, "vector count");
    std::size_t commands = 0, results = 0;
    for (const auto& row : rows) {
        if (row.type == "command") {
            ++commands;
            round_trip(row, decode_direct_configuration_command, encode_direct_configuration_command);
        } else if (row.type == "result") {
            ++results;
            round_trip(row, decode_direct_configuration_result, encode_direct_configuration_result);
        } else {
            check(false, row.name + " type");
        }
    }
    fields();
    if (failures) return 1;
    std::printf("PASS %zu direct configuration vectors (%zu command, %zu result) and field groups\n",
                rows.size(), commands, results);
    return 0;
}
