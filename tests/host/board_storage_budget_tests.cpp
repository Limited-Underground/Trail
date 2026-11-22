#include "board_storage_budget.hpp"

#include <cstdio>
#include <limits>

namespace budget = opentrail::target::heltec_v4_bench::board_storage_budget;

namespace {

constexpr auto kMax = std::numeric_limits<std::size_t>::max();
constexpr auto kRoomAtPlanningLimit = budget::kLimit - 1 - budget::kPlanningEntries;

static_assert(budget::blob_entries(1) == 4);
static_assert(budget::blob_entries(32) == 4);
static_assert(budget::blob_entries(33) == 5);
static_assert(budget::blob_entries(64) == 5);
static_assert(budget::blob_entries(kMax) == 4 + kMax / 32);

static_assert(budget::kRosterEntries == 136);
static_assert(budget::kCatalogEntries == 48);
static_assert(budget::kGroupPairEntries == 1'401);
static_assert(budget::kDirectEntries == 5'466);
static_assert(budget::kReceiptEntries == 360);
static_assert(budget::kStagingEntries == 512);
static_assert(budget::kResidentEntries == 11'549 && budget::kPlanningEntries == 12'421);
static_assert(kRoomAtPlanningLimit == 3'706);

static_assert(budget::can_write(budget::kPlanningEntries, kRoomAtPlanningLimit,
                                0, kRoomAtPlanningLimit, kRoomAtPlanningLimit));
static_assert(!budget::can_write(budget::kPlanningEntries, kRoomAtPlanningLimit + 1,
                                 0, kRoomAtPlanningLimit + 1, kRoomAtPlanningLimit + 1));
static_assert(budget::can_write(16'127, 5, 0, 0, 5));
static_assert(!budget::can_write(16'128, 5, 0, 0, 5));
static_assert(!budget::can_write(16'127, 5, 0, 1, 5));

static_assert(budget::can_write(16'126, 6, 1, 0, 5));
static_assert(!budget::can_write(16'126, 5, 1, 0, 5));
static_assert(!budget::can_write(16'127, 6, 1, 0, 5));
static_assert(!budget::can_write(0, 0, 1, 0, 0));
static_assert(!budget::can_write(1, 4, 0, 0, 5));

static_assert(!budget::can_write(kMax, kMax, 0, 0, 0));
static_assert(!budget::can_write(0, kMax, kMax, 0, 0));
static_assert(!budget::can_write(0, kMax, 0, kMax, 0));
static_assert(!budget::can_write(0, kMax, 1, 0, kMax));

}  // namespace

int main() {
    std::puts("PASS board storage budget model and admission boundary checks");
}
