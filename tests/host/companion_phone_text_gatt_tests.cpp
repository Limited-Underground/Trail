#include "companion_phone_text_gatt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace opentrail::target::heltec_v4_bench::phone_text_gatt;
using opentrail::radio::ByteView;

using Frame = std::array<std::uint8_t, 148>;
constexpr std::uint32_t kNonce = 0x03020104;

void expect(bool ok, const char* message) {
    if (ok) return;
    std::cerr << message << '\n';
    std::exit(1);
}

Frame frame(std::uint8_t kind, std::size_t size, std::uint8_t exchange = 7) {
    Frame result{};
    result[0] = 'O'; result[1] = 'T'; result[2] = 'C'; result[3] = '0';
    result[5] = 6; result[6] = kind;
    result[8] = 4; result[9] = 1; result[10] = 2; result[11] = 3;
    result[12] = exchange;
    result[17] = 1; result[18] = static_cast<std::uint8_t>(size - 20);
    result[20] = 'O'; result[21] = 'T';
    const bool authority = kind == 9 || kind == 0x8b;
    result[22] = authority ? 'Y' : 'X'; result[23] = authority ? '0' : 'C';
    return result;
}

Header checked(std::uint8_t byte, std::size_t size, Kind want) {
    const auto data = frame(byte, size);
    auto h = decode(ByteView{data.data(), size});
    expect(h.kind == want && h.nonce == kNonce && h.exchange == 7, "exact routing header");
    return h;
}

void typed_routes() {
    const auto query = checked(9, 76, Kind::authority_query);
    const auto proof = checked(0x8b, 136, Kind::authority_result);
    const auto send = checked(0x0a, 148, Kind::send);
    const auto result = checked(0x8c, 68, Kind::send_result);
    checked(0x8d, 59, Kind::received);
    checked(0x8e, 72, Kind::delivery);
    expect(query.command() && send.command() && !result.command(), "separate Command records");
    expect(matches_response(query, proof) && matches_response(send, result), "same Command exchange");
    expect(!matches_response(query, result) && !matches_response(send, proof), "wrong response family");
    auto different = frame(0x8c, 68, 8);
    expect(!matches_response(send, decode({different.data(), 68})), "wrong response exchange");
    different = frame(0x8c, 68);
    different[8] ^= 1;
    expect(!matches_response(send, decode({different.data(), 68})), "wrong response nonce");
}

void refusals() {
    auto q = frame(9, 76);
    for (const auto offset : {0, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 22, 23}) {
        q[offset] ^= 0x40;
        expect(decode({q.data(), 76}).kind == Kind::invalid, "wrong header");
        q[offset] ^= 0x40;
    }
    expect(decode({}).kind == Kind::invalid && decode({q.data(), 75}).kind == Kind::invalid &&
           decode({q.data(), 149}).kind == Kind::invalid, "length check before parsing");
    q = frame(9, 75);
    expect(decode({q.data(), 75}).kind == Kind::invalid, "authority shape");
    q = frame(0x0a, 58);
    expect(decode({q.data(), 58}).kind == Kind::invalid, "text frame lower bound");
    q = frame(9, 76, 0);
    expect(decode({q.data(), 76}).kind == Kind::invalid, "no zero exchange");
    q = frame(9, 76);
    for (auto i = 8; i != 12; ++i) q[i] = 0;
    expect(decode({q.data(), 76}).kind == Kind::invalid, "no zero nonce");
}
}  // namespace

int main() {
    typed_routes();
    refusals();
    std::cout << "PASS 2 protected GATT phone-text routing groups\n";
}
