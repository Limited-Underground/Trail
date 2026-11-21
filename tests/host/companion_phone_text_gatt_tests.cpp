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

void quick_status_routes() {
    constexpr auto quick = Payload::quick_status;
    auto request = frame(9, 76);
    auto proof = frame(0x8b, 136);
    expect(decode({request.data(), 76}, quick).kind == Kind::invalid &&
           decode({proof.data(), 136}, quick).kind == Kind::invalid, "F7 proof refused by F6 mode");
    const auto ordinary_request = request;
    request[22] = 'Q';
    request[23] = 'Y';
    Frame normalized{};
    expect(normalize_quick_status_command({request.data(), 76}, {normalized.data(), normalized.size()}) &&
           normalized == ordinary_request, "normalize only query magic");
    expect(mark_quick_status_output({proof.data(), 136}), "mark F6 proof");
    const auto routed_query = decode({request.data(), 76}, quick);
    const auto routed_proof = decode({proof.data(), 136}, quick);
    expect(routed_query.kind == Kind::authority_query && routed_proof.kind == Kind::authority_result &&
           matches_response(routed_query, routed_proof), "correlated F6 proof");
    expect(decode({request.data(), 76}).kind == Kind::invalid &&
           decode({proof.data(), 136}).kind == Kind::invalid, "F6 proof refused by F7 mode");

    for (const auto kind : {Kind::send, Kind::received}) {
        auto record = frame(static_cast<std::uint8_t>(kind), 59);
        record[58] = '1';
        expect(decode({record.data(), 59}, quick).kind == Kind::invalid, "typed data refused by F6 mode");
        const auto ordinary = record;
        if (kind == Kind::received) {
            expect(mark_quick_status_output({record.data(), 59}), "mark F6 receipt");
        } else {
            record[22] = 'Q';
            record[23] = 'S';
            normalized = {};
            expect(normalize_quick_status_command({record.data(), 59}, {normalized.data(), normalized.size()}) &&
                   normalized == ordinary, "normalize only status send magic");
        }
        expect(decode({record.data(), 59}).kind == Kind::invalid, "status data refused by F7 mode");
        for (auto status = '1'; status <= '4'; ++status) {
            record[58] = static_cast<std::uint8_t>(status);
            const auto route = decode({record.data(), 59}, quick);
            expect(route.kind == kind && route.nonce == kNonce && route.exchange == 7, "exact status route");
            expect(quick_status_code({record.data() + 58, 1}) == status - '0', "exact status code");
        }
        record[58] = '5';
        expect(decode({record.data(), 59}, quick).kind == Kind::invalid, "unknown status");
        record[58] = '3';
        record[18] = 40;
        expect(decode({record.data(), 60}, quick).kind == Kind::invalid, "status body requires exactly one byte");
    }
    const auto outcome = frame(0x8c, 68);
    const auto delivery = frame(0x8e, 72);
    expect(decode({outcome.data(), 68}, quick).kind == Kind::send_result &&
           decode({delivery.data(), 72}, quick).kind == Kind::delivery, "reuse correlated result and delivery");
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
    quick_status_routes();
    refusals();
    std::cout << "PASS 3 protected GATT phone-text routing groups (including quick-status mode)\n";
}
