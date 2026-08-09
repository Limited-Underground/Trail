#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "opentrail/packet_codec.hpp"

namespace {

std::uint32_t parse_u32(const char* text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (text[consumed] != '\0' || value > 0xFFFFFFFFULL) {
        throw std::invalid_argument("invalid 32-bit integer");
    }
    return static_cast<std::uint32_t>(value);
}

std::string to_hex(const std::uint8_t* bytes, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return output.str();
}

std::vector<std::uint8_t> from_hex(const std::string& text) {
    if (text.size() % 2 != 0) {
        throw std::invalid_argument("hex input must have an even length");
    }
    std::vector<std::uint8_t> bytes(text.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto pair = text.substr(index * 2, 2);
        std::size_t consumed = 0;
        const auto value = std::stoul(pair, &consumed, 16);
        if (consumed != pair.size()) {
            throw std::invalid_argument("invalid hex input");
        }
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return bytes;
}

int encode(int argc, char** argv) {
    if (argc != 6) {
        throw std::invalid_argument(
            "encode requires source-id network-id message-id payload-text");
    }
    const std::string payload = argv[5];
    const opentrail::protocol::PacketView packet{
        {
            0,
            opentrail::protocol::PacketType::experimental_probe,
            0,
            parse_u32(argv[2]),
            parse_u32(argv[3]),
            parse_u32(argv[4]),
        },
        {
            reinterpret_cast<const std::uint8_t*>(payload.data()),
            payload.size(),
        },
    };
    std::vector<std::uint8_t> encoded(
        opentrail::protocol::kPacketOverheadBytes + payload.size());
    const auto result = opentrail::protocol::encode_packet(
        packet,
        {encoded.data(), encoded.size()});
    if (!result.encoded()) {
        std::cerr << "encode error " << static_cast<int>(result.error) << '\n';
        return 2;
    }
    std::cout << to_hex(encoded.data(), result.encoded_bytes) << '\n';
    return 0;
}

int decode(int argc, char** argv) {
    if (argc != 3) {
        throw std::invalid_argument("decode requires one hex frame");
    }
    const auto encoded = from_hex(argv[2]);
    const auto result = opentrail::protocol::decode_packet(
        {encoded.data(), encoded.size()});
    if (!result.decoded()) {
        std::cerr << "decode error " << static_cast<int>(result.error) << '\n';
        return 2;
    }
    std::cout
        << "{\"source_node_id\":" << result.packet.header.source_node_id
        << ",\"network_id\":" << result.packet.header.network_id
        << ",\"message_id\":" << result.packet.header.message_id
        << ",\"type\":" << static_cast<unsigned>(result.packet.header.type)
        << ",\"payload_hex\":\""
        << to_hex(result.packet.payload.data, result.packet.payload.size)
        << "\"}\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument("expected encode or decode");
        }
        const std::string command = argv[1];
        if (command == "encode") {
            return encode(argc, argv);
        }
        if (command == "decode") {
            return decode(argc, argv);
        }
        throw std::invalid_argument("unknown command");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
