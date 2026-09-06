#include "opentrail/companion_device_name_codec.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace opentrail::companion;
int failures = 0;
void expect(bool value, const char* expression, int line) {
    if (!value) { ++failures; std::cerr << "FAIL " << line << ": " << expression << '\n'; }
}
#define EXPECT(value) expect((value), #value, __LINE__)

DeviceNamePayload named(DeviceNameKind kind, std::uint64_t revision) {
    DeviceNamePayload value{};
    value.kind = kind; value.revision = revision; value.name_bytes = 4;
    std::copy_n("Alex", 4, value.name.begin());
    return value;
}

bool encodes(const DeviceNamePayload& value) {
    std::array<std::uint8_t, 112> bytes{};
    return encode_device_name_payload(value, bytes.data(), bytes.size()).encoded();
}

void golden_and_roundtrip() {
    const auto value = named(DeviceNameKind::write, 0x0102030405060708ULL);
    std::array<std::uint8_t, 112> bytes{};
    const auto encoded = encode_device_name_payload(value, bytes.data(), bytes.size());
    const std::array<std::uint8_t, 20> expected{
        'O','T','N','C',1,2,0,4,8,7,6,5,4,3,2,1,'A','l','e','x'};
    EXPECT(encoded.encoded()); EXPECT(encoded.encoded_bytes == expected.size());
    EXPECT(std::equal(expected.begin(), expected.end(), bytes.begin()));
    const auto decoded = decode_device_name_payload(bytes.data(), encoded.encoded_bytes);
    EXPECT(decoded.decoded()); EXPECT(decoded.value.revision == value.revision);
    EXPECT(decoded.value.name == value.name); EXPECT(decoded.value.kind == DeviceNameKind::write);
    auto trail = value;
    trail.name_bytes = 5;
    std::copy_n("Trail", 5, trail.name.begin());
    const std::array<std::uint8_t, 21> kotlin_fixture{
        0x4f,0x54,0x4e,0x43,1,2,0,5,8,7,6,5,4,3,2,1,0x54,0x72,0x61,0x69,0x6c};
    const auto fixture_result = encode_device_name_payload(trail, bytes.data(), bytes.size());
    EXPECT(fixture_result.encoded_bytes == kotlin_fixture.size());
    EXPECT(std::equal(kotlin_fixture.begin(), kotlin_fixture.end(), bytes.begin()));
}

void semantic_matrix() {
    DeviceNamePayload empty{};
    EXPECT(encodes(empty));
    empty.revision = 1; EXPECT(!encodes(empty)); empty.revision = 0;
    empty.kind = DeviceNameKind::snapshot; EXPECT(encodes(empty));
    empty.revision = 1; EXPECT(!encodes(empty));
    EXPECT(encodes(named(DeviceNameKind::write, 0)));
    EXPECT(!encodes(named(DeviceNameKind::write, std::numeric_limits<std::uint64_t>::max())));
    EXPECT(!encodes(named(DeviceNameKind::snapshot, 0)));
    EXPECT(encodes(named(DeviceNameKind::snapshot, 1)));
    EXPECT(!encodes(named(DeviceNameKind::applied, 0)));
    EXPECT(encodes(named(DeviceNameKind::applied, std::numeric_limits<std::uint64_t>::max())));
    empty.revision = 0; empty.kind = DeviceNameKind::rejected;
    EXPECT(!encodes(empty));
    for (std::uint8_t reason = 1; reason <= 5; ++reason) {
        empty.reason = static_cast<DeviceNameReason>(reason); EXPECT(encodes(empty));
    }
    empty.kind = DeviceNameKind::uncertain;
    EXPECT(!encodes(empty));
    empty.reason = DeviceNameReason::storage_failure; EXPECT(encodes(empty));
    empty.revision = 1; EXPECT(!encodes(empty));
    empty.revision = 0; empty.reason = static_cast<DeviceNameReason>(6); EXPECT(!encodes(empty));
    empty.reason = DeviceNameReason::none; empty.kind = static_cast<DeviceNameKind>(3); EXPECT(!encodes(empty));
}

void malformed_envelopes_and_nonmutation() {
    std::array<std::uint8_t, 113> bytes{};
    const auto result = encode_device_name_payload(named(DeviceNameKind::write, 0), bytes.data(), bytes.size());
    EXPECT(result.encoded());
    for (std::size_t size = 0; size < result.encoded_bytes; ++size) {
        EXPECT(!decode_device_name_payload(bytes.data(), size).decoded());
    }
    EXPECT(!decode_device_name_payload(bytes.data(), result.encoded_bytes + 1).decoded());
    EXPECT(!decode_device_name_payload(bytes.data(), bytes.size()).decoded());
    EXPECT(!decode_device_name_payload(nullptr, 0).decoded());
    auto corrupted = bytes; corrupted[0] = 0; EXPECT(!decode_device_name_payload(corrupted.data(), 20).decoded());
    corrupted = bytes; corrupted[4] = 2; EXPECT(!decode_device_name_payload(corrupted.data(), 20).decoded());
    corrupted = bytes; corrupted[5] = 0xff; EXPECT(!decode_device_name_payload(corrupted.data(), 20).decoded());
    corrupted = bytes; corrupted[6] = 6; EXPECT(!decode_device_name_payload(corrupted.data(), 20).decoded());
    corrupted = bytes; corrupted[7] = 97; EXPECT(!decode_device_name_payload(corrupted.data(), 20).decoded());
    std::array<std::uint8_t, 112> sentinel{}; sentinel.fill(0x55); const auto before = sentinel;
    EXPECT(!encode_device_name_payload(named(DeviceNameKind::write, 0), sentinel.data(), 19).encoded());
    EXPECT(sentinel == before);
    EXPECT(!encode_device_name_payload(named(DeviceNameKind::read, 0), sentinel.data(), sentinel.size()).encoded());
    EXPECT(sentinel == before);
}

void utf8_boundaries() {
    const std::vector<std::vector<std::uint8_t>> invalid{
        {}, {' '}, {' ', 'A'}, {'A',' '}, {0}, {0x1f}, {0x7f}, {0xc2,0x80},
        {0xc2,0x9f}, {0x80}, {0xc0,0xaf}, {0xe0,0x80,0xaf},
        {0xf0,0x80,0x80,0xaf}, {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80},
        {0xf5,0x80,0x80,0x80}, {0xc2}, {0xe2,0x82}, {0xe2,'A',0xa0},
    };
    for (const auto& bytes : invalid) EXPECT(!valid_device_name_utf8(bytes.data(), bytes.size()));
    std::vector<std::uint8_t> ascii(32, 'A'); EXPECT(valid_device_name_utf8(ascii.data(), ascii.size()));
    ascii.push_back('A'); EXPECT(!valid_device_name_utf8(ascii.data(), ascii.size()));
    std::vector<std::uint8_t> bmp;
    for (int i = 0; i < 32; ++i) bmp.insert(bmp.end(), {0xe4,0xb8,0xad});
    EXPECT(bmp.size() == 96); EXPECT(valid_device_name_utf8(bmp.data(), bmp.size()));
    std::vector<std::uint8_t> astral;
    for (int i = 0; i < 16; ++i) astral.insert(astral.end(), {0xf0,0x9f,0x98,0x80});
    EXPECT(valid_device_name_utf8(astral.data(), astral.size()));
    astral.push_back('A'); EXPECT(!valid_device_name_utf8(astral.data(), astral.size()));
    DeviceNamePayload maximum{}; maximum.kind = DeviceNameKind::write; maximum.name_bytes = 96;
    std::copy(bmp.begin(), bmp.end(), maximum.name.begin()); EXPECT(encodes(maximum));
    std::array<std::uint8_t, 112> encoded{};
    EXPECT(encode_device_name_payload(maximum, encoded.data(), encoded.size()).encoded_bytes == 112);
    EXPECT(decode_device_name_payload(encoded.data(), encoded.size()).decoded());
}

std::vector<std::uint8_t> unhex(const std::string& text) {
    if (text == "-") return {};
    EXPECT(text.size() % 2 == 0);
    std::vector<std::uint8_t> result;
    auto digit = [](char value) -> unsigned {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
        EXPECT(false);
        return 0;
    };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>((digit(text[i]) << 4U) | digit(text[i + 1])));
    return result;
}

void shared_cross_language_corpus(const char* path) {
    std::ifstream input(path);
    EXPECT(input.is_open());
    if (!input.is_open()) return;
    std::set<std::string> names;
    std::size_t accepted_count = 0, rejected_count = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        EXPECT(line.size() <= 1'024);
        std::istringstream columns(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(columns, field, '\t')) fields.push_back(field);
        EXPECT(fields.size() == 7);
        if (fields.size() != 7) continue;
        EXPECT(!fields[0].empty() && names.insert(fields[0]).second);
        EXPECT(fields[1] == "0" || fields[1] == "1");
        const auto bytes = unhex(fields[2]);
        const auto decoded = decode_device_name_payload(bytes.data(), bytes.size());
        const bool accepted = fields[1] == "1";
        if (decoded.decoded() != accepted) std::cerr << "CORPUS " << fields[0] << '\n';
        EXPECT(decoded.decoded() == accepted);
        if (accepted) {
            ++accepted_count;
            if (!decoded.decoded()) continue;
            EXPECT(static_cast<unsigned>(decoded.value.kind) == std::stoul(fields[3]));
            EXPECT(static_cast<unsigned>(decoded.value.reason) == std::stoul(fields[4]));
            EXPECT(decoded.value.revision == std::stoull(fields[5]));
            const auto expected_name = unhex(fields[6]);
            EXPECT(decoded.value.name_bytes == expected_name.size());
            EXPECT(std::equal(expected_name.begin(), expected_name.end(), decoded.value.name.begin()));
            // Public golden semantics above prevent a mirrored endian bug from
            // passing merely because each codec can roundtrip its own result.
            std::array<std::uint8_t, kDeviceNameMaxPayloadBytes + 2> output{};
            output.fill(0xa5);
            const auto encoded = encode_device_name_payload(decoded.value, output.data() + 1, bytes.size());
            EXPECT(encoded.encoded() && encoded.encoded_bytes == bytes.size());
            EXPECT(std::equal(bytes.begin(), bytes.end(), output.begin() + 1));
            EXPECT(output.front() == 0xa5);
            EXPECT(std::all_of(output.begin() + 1 + bytes.size(), output.end(),
                [](std::uint8_t value) { return value == 0xa5; }));
        } else {
            ++rejected_count;
            EXPECT(fields[3] == "-" && fields[4] == "-" && fields[5] == "-" && fields[6] == "-");
            EXPECT(decoded.value.name_bytes == 0 && decoded.value.revision == 0);
            EXPECT(std::all_of(decoded.value.name.begin(), decoded.value.name.end(),
                [](std::uint8_t value) { return value == 0; }));
        }
    }
    EXPECT(accepted_count > 0 && rejected_count > 0);
    std::cout << "Shared corpus: " << accepted_count << " accepted, " << rejected_count << " rejected\n";
}
} // namespace

int main(int argc, char** argv) {
    golden_and_roundtrip(); semantic_matrix(); malformed_envelopes_and_nonmutation(); utf8_boundaries();
    shared_cross_language_corpus(argc > 1 ? argv[1] : "tests/fixtures/companion_device_name_v1.tsv");
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "PASS: 5 device-name payload codec groups (candidate only)\n";
    return EXIT_SUCCESS;
}
