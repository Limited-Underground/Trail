#include "pair_display.hpp"
#include "driver/gpio.h"
#include "esp_lcd_panel_ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
namespace opentrail::target::heltec_v4_pair_eval {
namespace {
std::array<std::uint8_t, 5> glyph_for(char value) {
    switch (value) {
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x7F, 0x20, 0x18, 0x20, 0x7F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x03, 0x04, 0x78, 0x04, 0x03};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case 'r': return {0x7C, 0x08, 0x04, 0x04, 0x08};
        case 'a': return {0x20, 0x54, 0x54, 0x54, 0x78};
        case 'i': return {0x00, 0x44, 0x7D, 0x40, 0x00};
        case 'l': return {0x00, 0x41, 0x7F, 0x40, 0x00};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
        default: return {0, 0, 0, 0, 0};
    }
}

void text(std::array<std::uint8_t,1024>& pixels, const char* value, unsigned y) {
    const auto count = std::strlen(value);
    const unsigned x0 = count <= 21 ? (128 - static_cast<unsigned>(count)*6)/2 : 0;
    for (unsigned n=0; n<count && n<21; ++n) {
        const auto glyph=glyph_for(value[n]);
        for (unsigned x=0; x<5; ++x)
            for (unsigned bit=0; bit<7; ++bit)
                if (glyph[x] & (1U<<bit)) pixels[((y+bit)/8)*128+x0+n*6+x] |= 1U<<((y+bit)%8);
    }
}
}
bool PairDisplay::initialize() {
    gpio_config_t power{}; power.pin_bit_mask=1ULL<<36; power.mode=GPIO_MODE_OUTPUT;
    if (gpio_config(&power)!=ESP_OK || gpio_set_level(GPIO_NUM_36,0)!=ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_master_bus_config_t bus{}; bus.i2c_port=-1; bus.sda_io_num=GPIO_NUM_17;
    bus.scl_io_num=GPIO_NUM_18; bus.clk_source=I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt=7; bus.flags.enable_internal_pullup=true;
    if (i2c_new_master_bus(&bus,&bus_)!=ESP_OK) return false;
    esp_lcd_panel_io_i2c_config_t io{}; io.dev_addr=0x3C; io.scl_speed_hz=400000;
    io.control_phase_bytes=1; io.dc_bit_offset=6; io.lcd_cmd_bits=8; io.lcd_param_bits=8;
    if (esp_lcd_new_panel_io_i2c(bus_,&io,&io_)!=ESP_OK) return false;
    esp_lcd_panel_ssd1306_config_t vendor{}; vendor.height=64;
    esp_lcd_panel_dev_config_t panel{}; panel.bits_per_pixel=1; panel.reset_gpio_num=GPIO_NUM_21;
    panel.vendor_config=&vendor; panel.flags.reset_active_high=false;
    if (esp_lcd_new_panel_ssd1306(io_,&panel,&panel_)!=ESP_OK ||
        esp_lcd_panel_reset(panel_)!=ESP_OK || esp_lcd_panel_init(panel_)!=ESP_OK ||
        esp_lcd_panel_mirror(panel_,true,true)!=ESP_OK ||
        esp_lcd_panel_invert_color(panel_,false)!=ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_,true)!=ESP_OK) return false;
    ready_=true;
    return draw("READY","","USB EVALUATION");
}
bool PairDisplay::draw(const char* status,const char* code,const char* instruction) {
    if (!ready_) return false;
    std::array<std::uint8_t,1024> pixels{};
    const char label[]{'R','O','L','E',' ',role_,0};
    text(pixels,label,0); text(pixels,status,16); text(pixels,code,32); text(pixels,instruction,48);
    if (esp_lcd_panel_draw_bitmap(panel_,0,0,128,64,pixels.data())==ESP_OK) return true;
    ready_=false;
    (void)esp_lcd_panel_disp_on_off(panel_,false);
    return false;
}
bool PairDisplay::show_review(security_evaluation::InvitationRole role,
                              const std::array<std::uint8_t,32>& transcript) {
    role_=role==security_evaluation::InvitationRole::initiator?'A':'B';
    constexpr char hex[]="0123456789ABCDEF";
    char code[9]{};
    for(unsigned i=0;i<4;++i){code[2*i]=hex[transcript[i]>>4];code[2*i+1]=hex[transcript[i]&15];}
    return draw("COMPARE CODE",code,"HOLD THEN RELEASE");
}
bool PairDisplay::show_state(security_evaluation::EndpointState state) {
    using State=security_evaluation::EndpointState;
    const char* label=state==State::local_confirmed?"LOCAL CONFIRMED":
        state==State::refused?"REFUSED":state==State::cancelled?"CLOSED":
        state==State::identity?"IDENTITY":state==State::handshake?"HANDSHAKE":"READY";
    return draw(label,"",state==State::local_confirmed?"NOT GROUP JOINED":"USB EVALUATION");
}
bool PairDisplay::show_failure(PairStage stage, PairError error) {
    const auto s=static_cast<unsigned>(stage), e=static_cast<unsigned>(error);
    if(s>99 || e>99) return false;
    const char code[]{'S',static_cast<char>('0'+s/10),static_cast<char>('0'+s%10),' ',
                      'E',static_cast<char>('0'+e/10),static_cast<char>('0'+e%10),0};
    return draw("REFUSED",code,"USB DIAG AVAILABLE");
}
}
