#include "led_controller.hpp"

namespace led_controller
{

LedController::LedController()
{
    FastLED.addLeds<APA102, config::kLedDataPin, config::kLedClockPin, BGR>(leds_, config::kNumLeds);
}

LedController::~LedController()
{

}

void LedController::update(const uint16_t *led_buff) 
{
    uint32_t led_state = 0;
    uint32_t led_index = 0;

    for (int i = 0; i < config::kNumLeds * 2; i = i + 2) {
        led_state  = static_cast<uint32_t>(led_buff[i] << 16);
        led_state |= static_cast<uint32_t>(led_buff[i + 1]);
        led_state &= 0x00FFFFFFU;
        leds_[led_index] = CRGB(led_state);
        led_index++;
    }
    
    FastLED.show();
}

void LedController::update(const uint32_t *led_buff) 
{
    for (int i = 0; i < config::kNumLeds; i++) {
        leds_[i] = CRGB(led_buff[i] & 0x00FFFFFFUL);
    }
    
    FastLED.show();
}

bool LedController::pool()
{
    return true;
}

} // namespace led_controller
