#include "companion_gatt_layout.hpp"

#include <cassert>
#include <iostream>

namespace layout = opentrail::target::heltec_v4_bench::companion_gatt_layout;

int main() {
    static_assert(layout::matches_current(0x08, 0x0e, 0x10, 0x11, 0x13, 0x15, 0x16, 0x18));
    static_assert(!layout::matches_current(0x08, 0x0e, 0x10, 0, 0x12, 0x14, 0x15, 0x17));
    static_assert(!layout::matches_current(0x08, 0x10, 0x12, 0x13, 0x15, 0x17, 0x18, 0x1a));

    layout::Migration legacy;
    assert(legacy.begin(1, true, true, true, false));
    assert(!legacy.begin(1, true, true, true, false));

    layout::Migration current;
    assert(!current.begin(1, true, true, false, true));
    assert(!current.begin(1, true, true, true, true));

    layout::Migration no_authority;
    assert(!no_authority.begin(1, false, true, true, false));
    assert(!no_authority.begin(1, true, false, true, false));
    assert(!no_authority.begin(1, true, true, false, false));
    assert(!no_authority.begin(layout::kNoConnection, true, true, true, false));

    assert(!legacy.confirm(2, 0x08, true, true));
    assert(!legacy.confirm(1, 0x15, true, true));
    assert(!legacy.confirm(1, 0x08, false, true));
    assert(!legacy.confirm(1, 0x08, true, false));
    legacy.disconnect(2);
    assert(!legacy.begin(2, true, true, true, false));
    legacy.disconnect(1);
    assert(!legacy.confirm(1, 0x08, true, true));
    assert(legacy.begin(2, true, true, true, false));

    assert(legacy.confirm(2, 0x08, true, true));
    legacy.disconnect(2);
    assert(!legacy.begin(3, true, true, true, false));

    std::cout << "PASS 6 Companion GATT layout migration groups\n";
}
