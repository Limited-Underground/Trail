#include "opentrail/companion_text_carrier.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace carrier = opentrail::companion::text_carrier;
namespace packet = opentrail::protocol::v1;
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const std::string& detail) {
    if (!condition) {
        std::cerr << "FAIL TEXT-CARRIER-V1 " << detail << '\n';
        std::exit(1);
    }
}

packet::View view(const Bytes& bytes) {
    return {bytes.data(), bytes.size()};
}

Bytes hex_bytes(const std::string& text) {
    expect(!text.empty() && text.size() % 2 == 0 &&
        text.find_first_not_of("0123456789abcdef") == std::string::npos, "invalid TSV hex");
    Bytes bytes;
    for (std::size_t index = 0; index < text.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

void check_fields(const std::string& id, const carrier::Record& record) {
    const auto header = std::visit([](const auto& value) { return value.header; }, record);
    if (id == "T-0065-send-ascii") {
        const auto* send = std::get_if<carrier::Send>(&record);
        expect(send && header.reference.nonce == 0x01020304 &&
            header.reference.authority_exchange == 0x05060708 &&
            header.reference.generation == 3 && header.exchange == 0x05060709 &&
            send->epoch == 2 && send->operation == 1 && send->text.size == 11, id);
    } else if (id == "T-0065-result-accepted") {
        const auto* result = std::get_if<carrier::Result>(&record);
        expect(result && result->operation == 1 && result->disposition == 1 && result->reason == 0 &&
            result->receipt == 100 && result->message_id == 0x1001 && result->counter == 1, id);
    } else if (id == "T-0065-received-ascii") {
        const auto* received = std::get_if<carrier::Received>(&record);
        expect(received && header.exchange == 1 && received->message_id == 0x1001 &&
            received->counter == 1 && received->text.size == 11, id);
    } else if (id == "T-0065-delivery-3") {
        const auto* delivery = std::get_if<carrier::Delivery>(&record);
        expect(delivery && header.exchange == 2 && delivery->origin_exchange == 0x05060709 &&
            delivery->operation == 1 && delivery->receipt == 100 && delivery->message_id == 0x1001 &&
            delivery->counter == 1 && delivery->phase == 3 && delivery->reason == 0, id);
    }
}

void shared_vectors(const std::filesystem::path& path) {
    std::ifstream fixture(path);
    expect(fixture.is_open(), "open shared TSV");
    std::set<std::string> ids;
    std::size_t golden = 0, malformed = 0, context_only = 0;
    for (std::string line; std::getline(fixture, line);) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream row(line);
        std::string id, layer, refusal, hex;
        expect(static_cast<bool>(std::getline(row, id, '\t') && std::getline(row, layer, '\t') &&
            std::getline(row, refusal, '\t') && std::getline(row, hex)) &&
            !id.empty() && !refusal.empty() && ids.insert(id).second, "invalid TSV row");
        const auto bytes = hex_bytes(hex);
        const auto record = carrier::decode(view(bytes), true);
        if (layer == "shape") {
            ++malformed;
            expect(!record, id);
            continue;
        }
        expect(layer == "golden" || layer == "context", id + " unknown layer");
        expect(record.has_value(), id + " wire admission");
        // Context-only faults have valid bytes. A codec cannot grant any live authority.
        if (layer == "context") {
            ++context_only;
        } else {
            ++golden;
            check_fields(id, *record);
        }
        std::array<std::uint8_t, carrier::max_record_bytes> output{};
        output.fill(0x7e);
        const auto original = output;
        expect(!carrier::decode(view(bytes)) &&
            carrier::encode(*record, {output.data(), output.size()}) == 0 &&
            output == original, id + " default profile disabled");
        expect(carrier::encode(*record, {output.data(), output.size()}, true) == bytes.size() &&
            std::equal(bytes.begin(), bytes.end(), output.begin()), id + " byte-exact round trip");
    }
    expect(fixture.eof() && golden == 13 && malformed == 14 && context_only == 9 && ids.size() == 36,
        "shared vector coverage");
}

void boundaries() {
    const carrier::Header header{{1, 2, 3}, 4};
    using Output = std::array<std::uint8_t, carrier::max_record_bytes>;
    Output output{};
    output.fill(0x7e);
    const auto original = output;
    for (const Bytes& unit : {Bytes{'x'}, Bytes{0xe7, 0x95, 0x8c}, Bytes{0xf0, 0x9f, 0x9a, 0xb2}}) {
        for (const std::size_t count : {89, 90, 91}) {
            Bytes text;
            while (text.size() + unit.size() <= count) {
                text.insert(text.end(), unit.begin(), unit.end());
            }
            text.resize(count, 'x');
            for (const carrier::Record& record : {
                    carrier::Record{carrier::Send{header, 2, 1, view(text)}},
                    carrier::Record{carrier::Received{header, 1, 1, view(text)}}}) {
                output = original;
                const auto size = carrier::encode(record, {output.data(), output.size()}, true);
                if (count == 91) {
                    expect(size == 0 && output == original, "91 UTF-8 bytes refuse before output");
                } else {
                    expect(size == 58 + count && carrier::decode({output.data(), size}, true).has_value(),
                        "89 and 90 UTF-8 byte records");
                }
            }
        }
    }
    expect(carrier::max_record_bytes + 3 == carrier::minimum_att_mtu, "ATT overhead");

    const Bytes valid{'h'};
    const Bytes invalid{0xed, 0xa0, 0x80};
    output = original;
    for (const carrier::Record& bad : {
            carrier::Record{carrier::Send{header, 2, 1, {}}},
            carrier::Record{carrier::Send{header, 2, 0, view(valid)}},
            carrier::Record{carrier::Received{header, 1, 1, view(invalid)}},
            carrier::Record{carrier::Received{header, 1, 0, view(valid)}},
            carrier::Record{carrier::Result{header, 1, 1, 0, 5, 2, 0}},
            carrier::Record{carrier::Result{header, 1, 2, 1, 5, 2, 3}},
            carrier::Record{carrier::Delivery{header, 5, 1, 5, 2, 3, 4, 0}},
            carrier::Record{carrier::Delivery{header, 5, 1, 5, 2, 3, 6, 0}}}) {
        expect(carrier::encode(bad, {output.data(), output.size()}, true) == 0 &&
            output == original, "invalid record preserves output");
    }
    const carrier::Record send = carrier::Send{header, 2, 1, view(valid)};
    expect(carrier::encode(send, {output.data(), 58}, true) == 0 && output == original,
        "short output preserves bytes");
    output[0] = 'h';
    const carrier::Record overlapping = carrier::Send{header, 2, 1, {output.data(), 1}};
    const auto size = carrier::encode(overlapping, {output.data(), output.size()}, true);
    const auto decoded = carrier::decode({output.data(), size}, true);
    const auto* decoded_send = decoded ? std::get_if<carrier::Send>(&*decoded) : nullptr;
    expect(size == 59 && decoded_send && decoded_send->text.size == 1 &&
        decoded_send->text.data[0] == 'h', "overlapping encode preserves text");
}
}

int main() {
    const auto path = std::filesystem::path(__FILE__).parent_path() /
        "../../android/protocol/src/test/resources/text-carrier-v1-vectors.tsv";
    shared_vectors(path);
    boundaries();
    std::cout << "PASS TEXT-CARRIER-V1 13 golden, 14 structural refusal, and 9 context-only vectors; boundary checks passed\n";
}
