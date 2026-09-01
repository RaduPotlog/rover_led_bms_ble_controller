#ifndef LED_CONTROLLER_HPP
#define LED_CONTROLLER_HPP

#include <Arduino.h>
#include <FastLED.h>

#include "config.hpp"

namespace led_controller
{

/// @brief Drives the APA102 strip on config::kLedDataPin / config::kLedClockPin.
class LedController 
{

public:

    /// @brief Constructor. Registers the strip with FastLED.
    explicit LedController();

    /// @brief Destructor.
    virtual ~LedController();

    /// @brief Update the strip from a packed 16-bit buffer.
    /// @param led_buff config::kNumLeds * 2 halves, each pair forming one 24-bit colour.
    void update(const uint16_t *led_buff);

    /// @brief Update the strip from a 32-bit colour buffer.
    /// @param led_buff config::kNumLeds entries, low 24 bits used.
    void update(const uint32_t *led_buff);

    /// @brief Periodic service hook.
    /// @return true.
    bool pool();
    
private:

    /// @brief FastLED frame buffer.
    CRGB leds_[config::kNumLeds];

    /// @brief Scratch buffer for packed 16-bit updates.
    uint16_t led_buff_temp[config::kNumLeds * 2] = {0};
};

} // namespace led_controller

#endif // LED_CONTROLLER_HPP
