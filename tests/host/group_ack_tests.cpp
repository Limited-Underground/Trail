#include "opentrail/group_ack.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

using namespace opentrail::delivery;
using opentrail::persistence::PersistentKvBackend;
using opentrail::persistence::PersistentKvBackendError;

namespace {

using KvError = PersistentKvBackendError;
using RxError = GroupReceiveStateError;

void expect(bool condition, const char* detail) {
    if (!condition) {
        std::cerr << detail << '\n';
        std::exit(1);
    }
}

enum class Cut { none, write, before_commit, after_commit, readback };

struct Media {
    std::vector<std::uint8_t> saved;
    unsigned writes{0};
    unsigned commits{0};
    unsigned verified_reads{0};
};

class Backend final : public PersistentKvBackend {
public:
    explicit Backend(Media& media) : media_(media) {}

    KvError read_blob(const char*, const char*, const char* key, std::uint8_t* out,
                      std::size_t capacity, std::size_t& actual) override {
        if (std::string_view(key) != "r00" || media_.saved.empty()) return KvError::not_found;
        if (awaiting_readback_) {
            if (cut == Cut::readback) return KvError::io_failure;
            awaiting_readback_ = false;
            ++media_.verified_reads;
        }
        actual = media_.saved.size();
        if (capacity < actual) return KvError::invalid_argument;
        std::copy(media_.saved.begin(), media_.saved.end(), out);
        return KvError::none;
    }

    KvError write_blob(const char*, const char*, const char* key,
                       const std::uint8_t* input, std::size_t size) override {
        expect(std::string_view(key) == "r00", "one sender uses only the first receive record");
        ++media_.writes;
        if (cut == Cut::write) return KvError::io_failure;
        pending_.assign(input, input + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        expect(false, "group ACK must never erase receive history");
        return KvError::io_failure;
    }

    KvError commit(const char*, const char*) override {
        if (cut == Cut::before_commit) return KvError::io_failure;
        media_.saved = pending_;
        ++media_.commits;
        awaiting_readback_ = true;
        return cut == Cut::after_commit ? KvError::io_failure : KvError::none;
    }

    Cut cut{Cut::none};

private:
    Media& media_;
    std::vector<std::uint8_t> pending_;
    bool awaiting_readback_{false};
};

GroupMemberId identity(std::uint8_t tag) {
    GroupMemberId id{};
    id.back() = tag;
    return id;
}

const GroupView kCurrentRoster{0x10203040, {0xA1, 0xB2, 0xC3, 0xD4}, true};
const SenderStanding kMember{true, true};
const GroupAckRoute kDataRoute{{identity(1), 0x1234567889ABCDEFULL}, identity(2), identity(3), 7, identity(4)};
const GroupDataOperation kOperation{kDataRoute, 0x11223344, 0xFEDCBA9876543210ULL};
const GroupAckPlaintext kExpected{
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    0x10, 0x20, 0x30, 0x40, 0xA1, 0xB2, 0xC3, 0xD4,
};

GroupReceiptAndAck incoming(GroupAckReceiver& receiver, const GroupDataOperation& operation = kOperation,
                            std::uint32_t number = 11, std::string_view body = "message",
                            GroupView roster = kCurrentRoster, SenderStanding standing = kMember,
                            std::uint32_t version = kCurrentRoster.roster_version,
                            GroupDigestPrefix prefix = kCurrentRoster.digest_prefix,
                            GroupContent content = GroupContent::text) {
    std::array<std::uint8_t, kGroupPlaintextBytes> data{};
    GroupFrameError error{};
    const auto size = encode_group_frame(content, {version, prefix, number},
                                          reinterpret_cast<const std::uint8_t*>(body.data()), body.size(),
                                          data.data(), data.size(), error);
    expect(size > 0 && error == GroupFrameError::none, "test frame encodes");
    return receiver.receive(operation, standing, roster, content, data.data(), size);
}

void refusal(const GroupReceiptAndAck& result, RxError error) {
    expect(result.receive.error == error && !result.receive.disposition && !result.ack,
           "failed receive returns neither a disposition nor ACK plaintext");
}

void disposition(const GroupReceiptAndAck& result, GroupReceive outcome, bool ack_expected) {
    expect(result.receive.error == RxError::none && result.receive.disposition &&
               result.receive.disposition->outcome == outcome && result.ack.has_value() == ack_expected,
           "receive disposition determines whether a group ACK is available");
}

void golden_ack_and_board_receipt() {
    Media media;
    Backend backend(media);
    GroupAckReceiver receiver(backend, kDataRoute);
    refusal(incoming(receiver), RxError::missing);
    expect(!media.commits, "normal receive cannot initialize its history");
    expect(receiver.initialize() == RxError::none, "initialize a trusted first receive history");

    // A frame using an older roster is accepted. The ACK carries this receiver's current roster.
    const auto accepted = incoming(receiver, kOperation, 11, "message", kCurrentRoster, kMember,
                                   kCurrentRoster.roster_version - 1, {0, 1, 2, 3});
    disposition(accepted, GroupReceive::shown, true);
    expect(media.commits == 2 && media.verified_reads == 2, "receive was committed and read back before ACK");
    const auto& reply = *accepted.ack;
    expect(reply.payload == kExpected && reply.envelope.message_id == kOperation.message_id &&
               reply.envelope.fragment_index == 0 && reply.envelope.fragment_count == 1 &&
               reply.envelope.route.sender == kDataRoute.recipient &&
               reply.envelope.route.recipient == kDataRoute.sender &&
               reply.envelope.route.epoch == kDataRoute.epoch &&
               reply.envelope.route.transcript == kDataRoute.transcript,
           "golden 16-byte ACK and reversed route with the same message ID");
    const auto receipt = match_group_ack(kOperation, reply.envelope, reply.payload.data(), reply.payload.size());
    expect(receipt && receipt->board == kDataRoute.recipient &&
               receipt->operation.message_id == kOperation.message_id &&
               receipt->operation.counter == kOperation.counter &&
               receipt->roster_version == kCurrentRoster.roster_version &&
               receipt->digest_prefix == kCurrentRoster.digest_prefix,
           "matched ACK yields board receipt and verified receiver roster hint");
}

void power_cut_and_reopen() {
    for (const auto cut : {Cut::write, Cut::before_commit, Cut::after_commit, Cut::readback}) {
        Media media;
        {
            Backend backend(media);
            GroupAckReceiver receiver(backend, kDataRoute);
            expect(receiver.initialize() == RxError::none, "initialize interruption fixture");
            backend.cut = cut;
            refusal(incoming(receiver), RxError::storage_failure);
            backend.cut = Cut::none;
            refusal(incoming(receiver), RxError::storage_failure);
        }
        Backend rebooted(media);
        GroupAckReceiver reopened(rebooted, kDataRoute);
        const bool applied = cut == Cut::after_commit || cut == Cut::readback;
        expect(media.commits == (applied ? 2U : 1U), "observed exact effect of interrupted receive");
        const auto acknowledged = incoming(reopened);
        disposition(acknowledged, applied ? GroupReceive::duplicate : GroupReceive::shown, true);
        expect(acknowledged.ack->payload == kExpected && media.commits == 2,
               "retry issues ACK only after verified existing or newly committed history");
    }
}

void correlation_and_malformed_ack() {
    Media media;
    Backend backend(media);
    GroupAckReceiver receiver(backend, kDataRoute);
    expect(receiver.initialize() == RxError::none, "initialize correlation fixture");
    const auto result = incoming(receiver);
    disposition(result, GroupReceive::shown, true);
    const auto& ack = *result.ack;
    const auto rejected = [&](const GroupAckEnvelope& envelope, const GroupAckPlaintext& payload) {
        expect(!match_group_ack(kOperation, envelope, payload.data(), payload.size()),
               "mismatched acknowledgement cannot mark a board as having received a message");
    };
    auto changed = ack.envelope;
    changed.route.sender.back() ^= 0x80;  // leaves first 31 bytes identical
    rejected(changed, ack.payload);
    changed = ack.envelope;
    changed.route.recipient.back() ^= 0x80;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    changed.route.group.admin.back() ^= 0x80;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    changed.route.group.group_id ^= 0x8000000000000000ULL;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    ++changed.route.epoch;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    changed.route.transcript.back() ^= 0x80;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    ++changed.message_id;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    changed.fragment_index = 1;
    rejected(changed, ack.payload);
    changed.fragment_index = 0;
    changed.fragment_count = 2;
    rejected(changed, ack.payload);
    changed = ack.envelope;
    auto payload = ack.payload;
    payload.front() ^= 0x80;
    rejected(changed, payload);
    payload = ack.payload;
    std::fill_n(payload.begin() + 8, 4, 0);
    rejected(changed, payload);
    for (const auto size : {0U, 8U, 15U, 17U}) {
        std::array<std::uint8_t, 17> frame{};
        std::copy(ack.payload.begin(), ack.payload.end(), frame.begin());
        expect(!match_group_ack(kOperation, ack.envelope, frame.data(), size), "exactly 16 bytes are required");
    }
    expect(!match_group_ack(kOperation, ack.envelope, nullptr, kGroupAckPlaintextBytes), "null input refused");
    auto no_pending_counter = kOperation;
    no_pending_counter.counter = 0;
    payload = ack.payload;
    std::fill_n(payload.begin(), 8, 0);
    expect(!match_group_ack(no_pending_counter, ack.envelope, payload.data(), payload.size()),
           "zero reference never identifies a pending operation");
}

void no_ack_for_rejected_data() {
    Media media;
    Backend backend(media);
    GroupAckReceiver receiver(backend, kDataRoute);
    expect(receiver.initialize() == RxError::none, "initialize refusal fixture");
    auto wrong_route = kOperation;
    auto wrong = kDataRoute;
    wrong.sender.back() ^= 0x80;
    wrong_route.route = wrong;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);
    wrong = kDataRoute;
    wrong.recipient.back() ^= 0x80;
    wrong_route.route = wrong;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);
    wrong = kDataRoute;
    wrong.group.admin.back() ^= 0x80;
    wrong_route.route = wrong;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);
    wrong = kDataRoute;
    wrong.group.group_id ^= 0x8000000000000000ULL;
    wrong_route.route = wrong;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);
    wrong_route = kOperation;
    ++wrong_route.route.epoch;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);
    wrong_route = kOperation;
    wrong_route.route.transcript.back() ^= 0x80;
    refusal(incoming(receiver, wrong_route), RxError::invalid_scope);

    auto bad_operation = kOperation;
    bad_operation.message_id = 0;
    refusal(incoming(receiver, bad_operation), RxError::invalid_frame);
    bad_operation = kOperation;
    bad_operation.counter = 0;
    refusal(incoming(receiver, bad_operation), RxError::invalid_frame);
    auto no_roster = kCurrentRoster;
    no_roster.roster_version = 0;
    refusal(incoming(receiver, kOperation, 11, "message", no_roster), RxError::invalid_frame);
    refusal(receiver.receive(kOperation, kMember, kCurrentRoster, GroupContent::text, nullptr, 0),
            RxError::invalid_frame);
    disposition(incoming(receiver, kOperation, 11, "message", kCurrentRoster, {false, true}),
                GroupReceive::not_member, false);
    disposition(incoming(receiver, kOperation, 11, "message", kCurrentRoster, {true, false}),
                GroupReceive::no_pair, false);
    disposition(incoming(receiver, kOperation, 11, "message", kCurrentRoster, kMember,
                         kCurrentRoster.roster_version + 1), GroupReceive::roster_newer, false);
    disposition(incoming(receiver, kOperation, 11, "message", kCurrentRoster, kMember,
                         kCurrentRoster.roster_version, {9}), GroupReceive::roster_differs, false);
    auto location_off = kCurrentRoster;
    location_off.location_allowed = false;
    disposition(incoming(receiver, kOperation, 11, "position", location_off, kMember,
                         kCurrentRoster.roster_version, kCurrentRoster.digest_prefix, GroupContent::position),
                GroupReceive::position_refused, false);
    expect(media.writes == 1 && media.commits == 1, "refused input never writes the durable window");

    disposition(incoming(receiver), GroupReceive::shown, true);
    auto later = kOperation;
    ++later.message_id;
    ++later.counter;
    disposition(incoming(receiver, later, 11, "different"), GroupReceive::conflict, false);
    ++later.message_id;
    ++later.counter;
    disposition(incoming(receiver, later, 43, "advanced"), GroupReceive::shown, true);
    ++later.message_id;
    ++later.counter;
    disposition(incoming(receiver, later, 11, "message"), GroupReceive::too_old, false);

    media.saved.back() ^= 1;
    ++later.message_id;
    ++later.counter;
    refusal(incoming(receiver, later, 44), RxError::invalid_record);
    expect(media.writes == 3 && media.commits == 3, "conflict, old and corrupt input never writes");
}

}  // namespace

int main() {
    golden_ack_and_board_receipt();
    power_cut_and_reopen();
    correlation_and_malformed_ack();
    no_ack_for_rejected_data();
    std::cout << "group ACK: 4 scenario groups passed\n";
}
