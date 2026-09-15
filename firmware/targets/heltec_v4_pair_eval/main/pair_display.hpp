#pragma once
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "opentrail/pair_bench_session.hpp"
#include "pair_diagnostics.hpp"
namespace opentrail::target::heltec_v4_pair_eval {
class PairDisplay final : public security_evaluation::PairBenchDisplay {
public:
    bool initialize();
    bool show_review(security_evaluation::InvitationRole role,
                     const std::array<std::uint8_t,32>& transcript) override;
    bool show_state(security_evaluation::EndpointState state) override;
    bool show_failure(PairStage stage, PairError error);
private:
    bool draw(const char* status, const char* code, const char* instruction);
    i2c_master_bus_handle_t bus_{nullptr};
    esp_lcd_panel_io_handle_t io_{nullptr};
    esp_lcd_panel_handle_t panel_{nullptr};
    bool ready_{false};
    char role_{'?'};
};
}
