// Host tests for the LoRa first-pairing frame (V1 F5, step R2a):
// firmware/components/protocol/include/opentrail/pairing_frame.hpp.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "opentrail/pairing_frame.hpp"

using namespace opentrail::protocol::pairing;

namespace {
int failures = 0;
#define EXPECT(condition)                                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++failures;                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
        }                                                                  \
    } while (false)

constexpr FrameType kAll[] = {
    FrameType::candidate,          FrameType::offer,
    FrameType::possession,         FrameType::clock_mark,
    FrameType::invitation_payload, FrameType::invitation_signature,
    FrameType::invitation_cosign,  FrameType::handshake,
    FrameType::control,            FrameType::status,
};

std::vector<std::uint8_t> payload_for(FrameType type) {
    std::vector<std::uint8_t> p(payload_bytes(type));
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = static_cast<std::uint8_t>(i * 7 + 3);
    return p;
}

void test_layout_and_round_trip() {
    const auto p = payload_for(FrameType::candidate);
    FrameBuffer buf{};
    std::size_t n = 0;
    EXPECT(encode(FrameType::candidate, 123456, p.data(), p.size(), buf, n));
    EXPECT(n == kHeaderBytes + 40);
    EXPECT(buf[0] == 'O' && buf[1] == 'P');
    EXPECT(buf[2] == 0x11);  // version 1, type 1
    EXPECT(buf[3] == 0x40 && buf[4] == 0xE2 && buf[5] == 0x01);  // 123456 LE
    Frame f{};
    EXPECT(decode(buf.data(), n, 123456, f) == DecodeResult::ok);
    EXPECT(f.type == FrameType::candidate && f.size == 40);
    EXPECT(f.payload == buf.data() + kHeaderBytes);
    bool same = true;
    for (std::size_t i = 0; i < f.size; ++i) same = same && f.payload[i] == p[i];
    EXPECT(same);
}

void test_every_type_fits_the_radio() {
    for (const auto type : kAll) {
        const auto p = payload_for(type);
        EXPECT(!p.empty());
        FrameBuffer buf{};
        std::size_t n = 0;
        EXPECT(encode(type, 1, p.data(), p.size(), buf, n));
        EXPECT(n <= 255);  // product radio MTU
        Frame f{};
        EXPECT(decode(buf.data(), n, 1, f) == DecodeResult::ok);
        EXPECT(f.type == type);
    }
    EXPECT(kMaxFrameBytes == 194);
    // Signed invitation: payload and signature together match the session's
    // 188-byte payload and 64-byte signature.
    EXPECT(payload_bytes(FrameType::invitation_payload) == 188);
    EXPECT(payload_bytes(FrameType::invitation_signature) == 64);
}

void test_encode_refusals() {
    const auto p = payload_for(FrameType::possession);
    FrameBuffer buf{};
    std::size_t n = 0;
    EXPECT(!encode(FrameType::possession, 0, p.data(), p.size(), buf, n));
    EXPECT(!encode(FrameType::possession, 1000000, p.data(), p.size(), buf, n));
    EXPECT(!encode(FrameType::possession, 5, p.data(), p.size() - 1, buf, n));
    EXPECT(!encode(FrameType::possession, 5, nullptr, p.size(), buf, n));
    EXPECT(!encode(static_cast<FrameType>(0), 5, p.data(), p.size(), buf, n));
    EXPECT(!encode(static_cast<FrameType>(11), 5, p.data(), p.size(), buf, n));
}

void test_decode_refusals() {
    const auto p = payload_for(FrameType::offer);
    FrameBuffer buf{};
    std::size_t n = 0;
    EXPECT(encode(FrameType::offer, 42, p.data(), p.size(), buf, n));
    Frame f{};
    // Another pairing attempt nearby is ignored, not treated as an error.
    EXPECT(decode(buf.data(), n, 43, f) == DecodeResult::other_attempt);
    EXPECT(decode(buf.data(), n, 0, f) == DecodeResult::other_attempt);
    EXPECT(decode(buf.data(), n - 1, 42, f) == DecodeResult::bad_size);
    EXPECT(decode(buf.data(), 5, 42, f) == DecodeResult::not_pairing);
    EXPECT(decode(nullptr, n, 42, f) == DecodeResult::not_pairing);
    auto altered = buf;
    altered[0] = 'X';
    EXPECT(decode(altered.data(), n, 42, f) == DecodeResult::not_pairing);
    altered = buf;
    altered[2] = 0x22;  // version 2
    EXPECT(decode(altered.data(), n, 42, f) == DecodeResult::bad_version);
    altered = buf;
    altered[2] = 0x1F;  // version 1, unknown type 15
    EXPECT(decode(altered.data(), n, 42, f) == DecodeResult::bad_type);
    altered = buf;
    altered[2] = 0x13;  // type changed to possession: size no longer matches
    EXPECT(decode(altered.data(), n, 42, f) == DecodeResult::bad_size);
    // The R1 link-test beacon ("OTLT") is other traffic.
    const std::uint8_t beacon[] = {'O', 'T', 'L', 'T', 1, 0, 0, 0, 1, 0, 0, 0, 1};
    EXPECT(decode(beacon, sizeof(beacon), 42, f) == DecodeResult::not_pairing);
}
}  // namespace

int main() {
    test_layout_and_round_trip();
    test_every_type_fits_the_radio();
    test_encode_refusals();
    test_decode_refusals();
    if (failures != 0) {
        std::cerr << failures << " pairing frame check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 pairing frame scenario groups\n";
    return EXIT_SUCCESS;
}
