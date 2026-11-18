#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace opentrail::protocol::v1 {
// Isolated group64 host successor only; no production wire/profile allocation.
constexpr const char* host_profile="group64-host-successor-unselected";
constexpr std::size_t header_bytes=44, tag_bytes=16, aad_bytes=164, max_mtu=255, max_fragments=16;
enum class Type:std::uint8_t { data=1, ack=2 };
enum class Error { none, invalid_argument, malformed, unsupported_version, invalid_context, bounds };
struct View { const std::uint8_t* data=nullptr; std::size_t size=0; };
struct MutableView { std::uint8_t* data=nullptr; std::size_t size=0; };
struct Header { Type type=Type::data; std::uint32_t epoch=0; std::uint64_t sender_alias=0,recipient_alias=0; std::uint32_t message_id=0; std::uint64_t counter=0; std::uint8_t fragment_index=0,fragment_count=1; std::uint16_t ciphertext_bytes=0; };
struct Context { std::array<std::uint8_t,32> sender{},recipient{},transcript{}; std::uint64_t sender_alias=0,recipient_alias=0; std::uint64_t group=0; std::uint32_t epoch=0; std::uint8_t direction=0; };
struct Result { Error error=Error::invalid_argument; std::size_t encoded_bytes=0; };
struct DecodeResult { Error error=Error::malformed; Header header{}; View ciphertext{},tag{}; };
Error encode_header(const Header&,const Context&,std::array<std::uint8_t,44>&);
Error aad(const Header&,const Context&,std::array<std::uint8_t,aad_bytes>&);
Result assemble_frame(const Header&,const Context&,View ciphertext,View tag,MutableView,std::size_t mtu=255);
DecodeResult decode_frame(View,const Context&,std::size_t mtu=255);
Error validate_plaintext(const Header&,View);
constexpr std::size_t capacity(std::size_t mtu){return mtu>=60&&mtu<=255?mtu-60:0;}
}
