#pragma once

namespace opentrail::target::heltec_v4_bench {

// Retire all ot_state owners before factory reset erases the partition.
// False means unmount could not be confirmed and the caller must refuse the erase.
[[nodiscard]] bool retire_pairing_storage_for_reset();

}  // namespace opentrail::target::heltec_v4_bench
