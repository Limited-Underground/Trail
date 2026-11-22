#include "pairing_nvs_reset.hpp"

namespace opentrail::target::heltec_v4_bench {

bool retire_pairing_storage_for_reset() {
    // These SDK fixtures have no mounted pairing or group NVS owner.
    return true;
}

}  // namespace opentrail::target::heltec_v4_bench
