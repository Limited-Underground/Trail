#include "opentrail/device_inbox.hpp"
#include <algorithm>
#include <limits>
namespace opentrail::delivery {
namespace {
bool same(InboxScope a, InboxScope b) {
  return a.context == b.context && a.recipient == b.recipient &&
         a.epoch == b.epoch;
}

bool valid(InboxScope s) {
  return s.context && s.recipient && s.epoch;
}

bool valid(InboxPolicy p) {
  return p.records && p.records <= kInboxSlots && p.bytes &&
         p.bytes <= kInboxSlots * kInboxBodyBytes && p.ttl_ms &&
         p.dedupe_ms >= p.ttl_ms && p.full == InboxFullPolicy::reject_new;
}

bool same(InboxPolicy a, InboxPolicy b) {
  return a.records == b.records && a.bytes == b.bytes && a.ttl_ms == b.ttl_ms &&
         a.dedupe_ms == b.dedupe_ms && a.full == b.full;
}

bool valid(DuplicateKey k, InboxScope s) {
  return k.source_alias && k.message_id && k.group_epoch == s.epoch;
}

void age(InboxSnapshot& s, std::uint64_t elapsed) {
  std::size_t kept = 0;
  for (std::size_t i = 0; i < s.count; ++i) {
    auto r = s.records[i];
    if (elapsed >= r.dedupe_left) {
      continue;
    }
    r.dedupe_left -= static_cast<std::uint32_t>(elapsed);
    r.ttl_left = elapsed >= r.ttl_left
                     ? 0 : r.ttl_left - static_cast<std::uint32_t>(elapsed);
    s.records[kept++] = r;
  }
  for (auto i = kept; i < s.count; ++i) {
    s.records[i] = {};
  }
  s.count = kept;
}

bool valid(const InboxSnapshot& s) {
  if (s.version != 1 || !valid(s.scope) || !valid(s.policy) || !s.generation ||
      s.count > s.policy.records) {
    return false;
  }
  std::size_t bytes = 0;
  for (std::size_t i = 0; i < s.count; ++i) {
    const auto& r = s.records[i];
    if (!valid(r.key, s.scope) || !r.size || r.size > kInboxBodyBytes ||
        !r.dedupe_left || r.dedupe_left > s.policy.dedupe_ms ||
        r.ttl_left > s.policy.ttl_ms || r.ttl_left > r.dedupe_left ||
        (r.opened && !r.imported)) {
      return false;
    }
    if (std::any_of(r.body.begin() + r.size, r.body.end(),
                    [](std::uint8_t byte) { return byte != 0; })) {
      return false;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (s.records[j].key == r.key) {
        return false;
      }
    }
    bytes += r.size;
  }
  for (std::size_t i = s.count; i < kInboxSlots; ++i) {
    const auto& r = s.records[i];
    if (r.key.source_alias || r.key.group_epoch || r.key.message_id || r.size ||
        r.ttl_left || r.dedupe_left || r.imported || r.opened ||
        std::any_of(r.body.begin(), r.body.end(),
                    [](std::uint8_t byte) { return byte != 0; })) {
      return false;
    }
  }
  return bytes <= s.policy.bytes;
}
}  // namespace

DeviceInbox::DeviceInbox(InboxStorage& storage, InboxScope scope,
                         InboxPolicy policy) : storage_(storage) {
  live_.scope = scope;
  live_.policy = policy;
}

InboxError DeviceInbox::restore(time::MonotonicTime now, bool known,
                                std::uint64_t elapsed) {
  if (io_active_ || ready_ || fault_ || !valid(live_.scope) || !valid(live_.policy)) {
    return InboxError::invalid;
  }
  if (!now.ok()) {
    return InboxError::clock_fault;
  }
  InboxSnapshot stored{};
  io_active_ = true;
  const auto read = storage_.read(stored);
  io_active_ = false;
  if (read != InboxRead::found && read != InboxRead::empty) {
    return InboxError::storage_failure;
  }
  if (read == InboxRead::found) {
    if (!valid(stored) || !same(stored.scope, live_.scope) ||
        !same(stored.policy, live_.policy)) {
      return InboxError::invalid;
    }
    if (!known) {
      return InboxError::time_uncertain;
    }
    age(stored, elapsed);
  } else {
    stored = live_;
  }
  const auto result = save(stored, now.value_ms);
  if (result == InboxError::none) {
    ready_ = true;
  }
  return result;
}
InboxError DeviceInbox::prepare(time::MonotonicTime now, InboxSnapshot& next) {
  if (io_active_ || !ready_) {
    return InboxError::unavailable;
  }
  if (fault_) {
    return InboxError::reconcile_required;
  }
  if (!now.ok() || now.value_ms < last_now_) {
    fault_ = true;
    return InboxError::clock_fault;
  }
  last_now_ = now.value_ms;
  next = live_;
  age(next, now.value_ms - saved_at_);
  return InboxError::none;
}

InboxError DeviceInbox::save(InboxSnapshot& next, std::uint64_t now) {
  if (next.generation == std::numeric_limits<std::uint64_t>::max()) {
    fault_ = true;
    return InboxError::reconcile_required;
  }
  ++next.generation;
  io_active_ = true;
  const auto result = storage_.commit(next);
  io_active_ = false;
  if (result != InboxCommit::verified && result != InboxCommit::failed_before_commit) {
    fault_ = true;
    return InboxError::reconcile_required;
  }
  if (result != InboxCommit::verified) {
    return InboxError::storage_failure;
  }
  live_ = next;
  saved_at_ = last_now_ = now;
  return InboxError::none;
}

InboxResult DeviceInbox::receive(const InboxAdmission& a, time::MonotonicTime now) {
  if (!same(a.authorized_scope, live_.scope) || !valid(a.key, live_.scope) ||
      !a.body || !a.size || a.size > kInboxBodyBytes) {
    return {InboxError::invalid};
  }
  InboxSnapshot next{};
  auto error = prepare(now, next);
  if (error != InboxError::none) {
    return {error};
  }
  std::size_t bytes = 0;
  for (std::size_t i = 0; i < next.count; ++i) {
    const auto& r = next.records[i];
    bytes += r.size;
    if (r.key == a.key) {
      if (r.size != a.size || !std::equal(a.body, a.body + a.size, r.body.begin())) {
        return {InboxError::conflict};
      }
      // Expired content stays as a dedupe tombstone, not a fresh delivery ACK.
      if (!r.ttl_left) {
        return {InboxError::expired, false, true};
      }
      return {InboxError::none, true, true};
    }
  }
  if (next.count >= next.policy.records || a.size > next.policy.bytes - bytes) {
    return {InboxError::full};
  }
  auto& r = next.records[next.count++];
  r = {};
  r.key = a.key;
  r.size = a.size;
  std::copy(a.body, a.body + a.size, r.body.begin());
  r.ttl_left = next.policy.ttl_ms;
  r.dedupe_left = next.policy.dedupe_ms;
  error = save(next, now.value_ms);
  return {error, error == InboxError::none, false};
}

InboxError DeviceInbox::next_for_phone(InboxScope scope, time::MonotonicTime now,
                                      InboxRecord& output) {
  if (!same(scope, live_.scope)) {
    return InboxError::invalid;
  }
  InboxSnapshot next{};
  const auto error = prepare(now, next);
  if (error != InboxError::none) {
    return error;
  }
  for (std::size_t i = 0; i < next.count; ++i) {
    if (next.records[i].ttl_left && !next.records[i].imported) {
      output = next.records[i];
      return InboxError::none;
    }
  }
  return InboxError::missing;
}
InboxError DeviceInbox::phone_update(InboxScope scope, DuplicateKey key,
                                     time::MonotonicTime now, bool opened) {
  if (!same(scope, live_.scope)) {
    return InboxError::invalid;
  }
  InboxSnapshot next{};
  const auto error = prepare(now, next);
  if (error != InboxError::none) {
    return error;
  }
  for (std::size_t i = 0; i < next.count; ++i) {
    auto& r = next.records[i];
    if (!(r.key == key)) {
      continue;
    }
    if (!r.ttl_left) {
      return InboxError::expired;
    }
    if (opened && !r.imported) {
      return InboxError::invalid;
    }
    if (opened) {
      r.opened = true;
    } else {
      r.imported = true;
    }
    return save(next, now.value_ms);
  }
  return InboxError::missing;
}

InboxError DeviceInbox::phone_imported(InboxScope s, DuplicateKey k,
                                      time::MonotonicTime n) {
  return phone_update(s, k, n, false);
}

InboxError DeviceInbox::phone_opened(InboxScope s, DuplicateKey k,
                                    time::MonotonicTime n) {
  return phone_update(s, k, n, true);
}
} // namespace opentrail::delivery
