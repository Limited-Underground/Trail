#include "opentrail/device_inbox.hpp"
#include <cassert>
#include <iostream>
#include <functional>
#include <limits>
using namespace opentrail::delivery;
using opentrail::time::MonotonicTime;
using opentrail::time::MonotonicClockError;
namespace {
struct Store : InboxStorage {
  InboxSnapshot saved{};
  bool exists = false;
  bool apply_uncertain = false;
  bool invalid_read = false;
  unsigned writes = 0;
  std::function<void()> on_commit;
  InboxCommit outcome = InboxCommit::verified;
  InboxRead read(InboxSnapshot& output) override {
    output = saved;
    if (invalid_read) {
      return static_cast<InboxRead>(99);
    }
    return exists ? InboxRead::found : InboxRead::empty;
  }
  InboxCommit commit(const InboxSnapshot& value) override {
    ++writes;
    if (on_commit) {
      on_commit();
    }
    if (outcome == InboxCommit::verified || apply_uncertain) {
      saved = value;
      exists = true;
    }
    return outcome;
  }
};
InboxScope scope() {
  return {1, 2, 3};
}

InboxPolicy policy() {
  return {2, 8, 100, 200, InboxFullPolicy::reject_new};
}

MonotonicTime at(std::uint64_t n) {
  return {MonotonicClockError::none, n};
}

const std::uint8_t body[] = {1, 2, 3, 4};

InboxAdmission message(std::uint32_t id = 1) {
  return {scope(), {4, 3, id}, body, sizeof body};
}

void boot(DeviceInbox& inbox) {
  assert(inbox.restore(at(0), false, 0) == InboxError::none);
}

void policy_and_admission(){
  Store store;
  DeviceInbox absent(store,scope(),{});
  assert(absent.restore(at(0),true,0)==InboxError::invalid);
  auto bad = policy();
  bad.dedupe_ms = 99;
  DeviceInbox mismatch(store,scope(),bad);
  assert(mismatch.restore(at(0),true,0)==InboxError::invalid);
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  auto m = message();
  m.authorized_scope.recipient = 9;
  assert(inbox.receive(m,at(1)).error==InboxError::invalid);
  m = message();
  m.key.group_epoch = 9;
  assert(inbox.receive(m,at(1)).error==InboxError::invalid);
  m = message();
  m.size = kInboxBodyBytes + 1;
  assert(inbox.receive(m,at(1)).error==InboxError::invalid);
  assert(store.writes==1);
}
void commit_before_ack_and_duplicates(){
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  store.outcome=InboxCommit::failed_before_commit;
  const auto failed=inbox.receive(message(),at(1));
  assert(failed.error==InboxError::storage_failure&&!failed.storage_ack_candidate);
  assert(store.saved.count==0);
  store.outcome=InboxCommit::verified;
  const auto first=inbox.receive(message(),at(2));
  assert(first.storage_ack_candidate&&!first.duplicate&&store.saved.count==1);
  const auto writes=store.writes;
  const auto duplicate=inbox.receive(message(),at(3));
  assert(duplicate.storage_ack_candidate&&duplicate.duplicate&&store.writes==writes);
  auto m = message();
  const std::uint8_t other[] = {9, 2, 3, 4};
  m.body = other;
  assert(inbox.receive(m,at(4)).error==InboxError::conflict);
  assert(store.saved.records[0].body[0]==1);
}
void full_and_expiry(){
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  assert(inbox.receive(message(),at(0)).storage_ack_candidate);
  assert(inbox.receive(message(2),at(0)).storage_ack_candidate);
  assert(inbox.receive(message(3),at(1)).error==InboxError::full);
  assert(inbox.receive(message(),at(100)).error==InboxError::expired);
  assert(inbox.receive(message(3),at(100)).error==InboxError::full);
  InboxRecord untouched{};
  untouched.size = 42;
  assert(inbox.next_for_phone(scope(),at(100),untouched)==InboxError::missing);
  assert(untouched.size==42);
  store.outcome=InboxCommit::failed_before_commit;
  assert(!inbox.receive(message(3),at(200)).storage_ack_candidate);
  assert(store.saved.count==2);
  store.outcome=InboxCommit::verified;
  assert(inbox.receive(message(3),at(200)).storage_ack_candidate);
  assert(store.saved.count==1&&store.saved.records[0].key.message_id==3);
}
void uncertain_reboot(){
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  store.outcome = InboxCommit::uncertain;
  store.apply_uncertain = true;
  auto result=inbox.receive(message(),at(5));
  assert(result.error==InboxError::reconcile_required&&!result.storage_ack_candidate);
  assert(inbox.receive(message(),at(6)).error==InboxError::reconcile_required);
  store.outcome = InboxCommit::verified;
  store.apply_uncertain = false;
  DeviceInbox restart(store,scope(),policy());
  assert(restart.restore(at(0),false,0)==InboxError::time_uncertain);
  assert(restart.receive(message(),at(0)).error==InboxError::unavailable);
  assert(restart.restore(at(0),true,10)==InboxError::none);
  assert(store.saved.records[0].ttl_left==90);
  assert(restart.receive(message(),at(1)).duplicate);
  DeviceInbox elapsed(store,scope(),policy());
  assert(elapsed.restore(at(0),true,300)==InboxError::none);
  assert(store.saved.count==0);
}
void sync_is_not_read(){
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  assert(inbox.receive(message(),at(1)).storage_ack_candidate);
  InboxRecord record{};
  assert(inbox.next_for_phone(scope(),at(2),record)==InboxError::none);
  assert(!record.opened&&!record.imported);
  assert(inbox.phone_opened(scope(),record.key,at(2))==InboxError::invalid);
  assert(inbox.next_for_phone(scope(),at(3),record)==InboxError::none);
  auto wrong = scope();
  wrong.recipient = 99;
  assert(inbox.phone_imported(wrong,record.key,at(3))==InboxError::invalid);
  store.outcome=InboxCommit::failed_before_commit;
  assert(inbox.phone_imported(scope(),record.key,at(3))==InboxError::storage_failure);
  assert(inbox.next_for_phone(scope(),at(3),record)==InboxError::none);
  store.outcome=InboxCommit::verified;
  assert(inbox.phone_imported(scope(),record.key,at(4))==InboxError::none);
  assert(store.saved.records[0].imported&&!store.saved.records[0].opened);
  assert(inbox.next_for_phone(scope(),at(4),record)==InboxError::missing);
  DeviceInbox reboot(store, scope(), policy());
  assert(reboot.restore(at(0), true, 1) == InboxError::none);
  assert(reboot.next_for_phone(scope(),at(0),record)==InboxError::missing);
  assert(reboot.phone_opened(scope(),message(2).key,at(0))==InboxError::missing);
  assert(reboot.phone_opened(scope(),message().key,at(1))==InboxError::none);
  assert(store.saved.records[0].opened);
}
void corrupt_time_and_exhaustion(){
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  assert(inbox.receive(message(),at(10)).storage_ack_candidate);
  assert(inbox.receive(message(),at(9)).error==InboxError::clock_fault);
  assert(inbox.receive(message(),at(11)).error==InboxError::reconcile_required);
  store.saved.version=9;
  DeviceInbox bad(store, scope(), policy());
  assert(bad.restore(at(0), true, 0) == InboxError::invalid);
  store.saved.version = 1;
  store.saved.generation = std::numeric_limits<std::uint64_t>::max();
  DeviceInbox exhausted(store, scope(), policy());
  assert(exhausted.restore(at(0), true, 0) == InboxError::reconcile_required);
  Store high;
  DeviceInbox near_limit(high, scope(), policy());
  assert(near_limit.restore(at(std::numeric_limits<std::uint64_t>::max()-2),false,0)==InboxError::none);
  assert(near_limit.receive(message(),at(std::numeric_limits<std::uint64_t>::max()-1)).storage_ack_candidate);
  assert(near_limit.receive(message(),at(std::numeric_limits<std::uint64_t>::max())).duplicate);
}
void backend_reentry_is_refused() {
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  unsigned callbacks = 0;
  store.on_commit = [&] {
    ++callbacks;
    assert(inbox.receive(message(2), at(1)).error == InboxError::unavailable);
    InboxRecord output{};
    assert(inbox.next_for_phone(scope(), at(1), output) == InboxError::unavailable);
    assert(inbox.restore(at(1), true, 0) == InboxError::invalid);
  };
  assert(inbox.receive(message(), at(1)).storage_ack_candidate);
  assert(callbacks == 1 && store.saved.count == 1);
}

void unknown_storage_outcomes_fail_closed() {
  Store store;
  store.invalid_read = true;
  DeviceInbox inbox(store, scope(), policy());
  assert(inbox.restore(at(0), true, 0) == InboxError::storage_failure);
  assert(store.writes == 0);
  store.invalid_read = false;
  boot(inbox);
  store.outcome = static_cast<InboxCommit>(99);
  const auto result = inbox.receive(message(), at(1));
  assert(result.error == InboxError::reconcile_required);
  assert(!result.storage_ack_candidate);
  assert(inbox.receive(message(), at(2)).error == InboxError::reconcile_required);
}

void noncanonical_restore_rejected() {
  Store store;
  DeviceInbox inbox(store, scope(), policy());
  boot(inbox);
  assert(inbox.receive(message(), at(1)).storage_ack_candidate);
  const auto original = store.saved;
  store.saved.records[0].body[4] = 1;
  DeviceInbox tail(store, scope(), policy());
  assert(tail.restore(at(0), true, 0) == InboxError::invalid);
  store.saved = original;
  store.saved.records[1].body[0] = 1;
  DeviceInbox unused_body(store, scope(), policy());
  assert(unused_body.restore(at(0), true, 0) == InboxError::invalid);
  store.saved = original;
  store.saved.records[1].key.message_id = 1;
  DeviceInbox unused_key(store, scope(), policy());
  assert(unused_key.restore(at(0), true, 0) == InboxError::invalid);
  store.saved = original;
  store.saved.records[1].opened = true;
  DeviceInbox unused_flag(store, scope(), policy());
  assert(unused_flag.restore(at(0), true, 0) == InboxError::invalid);
}
}
int main() {
  policy_and_admission();
  commit_before_ack_and_duplicates();
  full_and_expiry();
  uncertain_reboot();
  sync_is_not_read();
  corrupt_time_and_exhaustion();
  noncanonical_restore_rejected();
  backend_reentry_is_refused();
  unknown_storage_outcomes_fail_closed();
  std::cout << "PASS 9 device inbox host groups\n";
}
