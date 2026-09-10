#include "opentrail/serialized_secure_random.hpp"
#include "heltec_v4_secure_random.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
using namespace opentrail::security;
using opentrail::target::heltec_v4_bench::HeltecV4SecureRandom;
static std::atomic<bool> physical{false};
static std::atomic<unsigned> calls{0};
static std::mutex mutex;
static std::condition_variable cv;
static bool block=false, entered=false, release_fill=false;
extern "C" void esp_fill_random(void* output, size_t size) {
    ++calls;
    std::unique_lock<std::mutex> lock(mutex);
    if(block){entered=true;cv.notify_all();cv.wait(lock,[]{return release_fill;});}
    for(size_t i=0;i<size;++i)static_cast<unsigned char*>(output)[i]=0x5a;
}
static bool probe(void*) noexcept { return physical.load(); }
int main() {
    HeltecV4SecureRandom actual;
    SerializedSecureRandomSource guard(actual,probe,nullptr);
    std::array<unsigned char,65> output{};
    // A caller-set ready flag cannot bypass the real-source readiness probe.
    actual.set_entropy_state(EntropyState::ready);
    assert(!guard.activate());assert(!guard.fill(output.data(),16).ok());assert(calls==0);
    physical=true;actual.set_entropy_state(EntropyState::not_ready);
    assert(!guard.activate());actual.set_entropy_state(EntropyState::failed);assert(!guard.activate());
    actual.set_entropy_state(EntropyState::ready);assert(guard.activate());
    assert(guard.state()==EntropyState::ready);
    assert(guard.fill(nullptr,1).error==RandomFillError::invalid_argument);
    assert(guard.fill(output.data(),0).error==RandomFillError::invalid_argument);
    assert(guard.fill(output.data(),65).error==RandomFillError::request_too_large);assert(calls==0);
    assert(guard.fill(output.data(),64).bytes_written==64&&calls==1&&output[63]==0x5a);
    // Stale ready is rejected even without an explicit owner transition.
    physical=false;output.fill(0xa5);assert(!guard.fill(output.data(),16).ok());assert(output[0]==0xa5&&calls==1);
    physical=true;assert(!guard.fill(output.data(),16).ok());assert(guard.revoke());assert(guard.activate());
    // Revoke closes admission immediately, but does not pretend the fill drained.
    {std::lock_guard<std::mutex> lock(mutex);block=true;}
    RandomFillResult in_flight;
    std::thread worker([&]{in_flight=guard.fill(output.data(),16);});
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(2),[]{return entered;}));}
    assert(!guard.revoke());assert(guard.state()==EntropyState::not_ready);
    std::array<unsigned char,16> other{};assert(!guard.fill(other.data(),other.size()).ok());assert(calls==2);
    {std::lock_guard<std::mutex> lock(mutex);release_fill=true;cv.notify_all();}
    worker.join();assert(in_flight.ok());assert(guard.revoke());physical=false;
    assert(!guard.fill(output.data(),16).ok());assert(calls==2);
    // Recovery requires a new explicit qualified activation.
    physical=true;assert(!guard.fill(output.data(),16).ok());assert(guard.activate());
    {std::lock_guard<std::mutex> lock(mutex);block=false;}
    assert(guard.fill(output.data(),16).ok());assert(guard.revoke());
    // An unexpected source loss during sampling discards all partial material.
    assert(guard.activate());output.fill(0xa5);
    {std::lock_guard<std::mutex> lock(mutex);block=true;entered=false;release_fill=false;}
    std::thread lost_source([&]{in_flight=guard.fill(output.data(),16);});
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(2),[]{return entered;}));}
    physical=false;
    {std::lock_guard<std::mutex> lock(mutex);release_fill=true;cv.notify_all();}
    lost_source.join();assert(in_flight.error==RandomFillError::entropy_failed);
    for(auto b:output)assert(b==0xa5);
    physical=true;assert(!guard.fill(output.data(),16).ok());assert(guard.revoke());
    SerializedSecureRandomSource missing(actual,nullptr,nullptr);assert(!missing.activate());
    // Partial/failing underlying writes never escape the scratch buffer.
    struct Broken final:SecureRandomSource {
        EntropyState state()const override{return EntropyState::ready;}
        RandomFillResult fill(std::uint8_t* p,std::size_t)override{p[0]=0;return {RandomFillError::none,1};}
    } broken;
    SerializedSecureRandomSource checked(broken,probe,nullptr);assert(checked.activate());output.fill(0xa5);
    assert(checked.fill(output.data(),16).error==RandomFillError::entropy_failed);
    for(auto b:output)assert(b==0xa5);
    assert(checked.state()==EntropyState::not_ready);
    std::puts("serialized entropy: actual adapter admission/bounds/source-loss/revoke-inflight/restart/fault isolation PASS");
}
