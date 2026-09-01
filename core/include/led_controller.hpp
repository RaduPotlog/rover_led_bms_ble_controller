#ifndef LED_CONTROLLER_HPP
#define LED_CONTROLLER_HPP

#include <Arduino.h>
#include <FastLED.h>

#include "config.hpp"

namespace led_controller
{

/// @brief Drives the APA102 strip on config::kLedDataPin / config::kLedClockPin.
///
/// @note Two execution contexts want to write this strip: loop(), which shows the
///       link-status animation while WiFi is down, and the AsyncUDP receive task,
///       which paints incoming colour frames. They previously both called
///       FastLED.show() directly, coordinated only by a volatile bool that gated
///       intent rather than access -- on a mode change both could be inside show()
///       at once, and FastLED is not reentrant.
///
///       Here there is exactly one writer. submit_frame() runs on the UDP task and
///       only stages bytes under a short critical section; poll() runs on the loop
///       task and is the only caller of FastLED.show().
class LedController
{

public:

    /// @brief Who the strip is currently showing.
    enum class Mode
    {
        LinkStatus, ///< loop() drives it: red blink while the link is down.
        Udp,        ///< Incoming UDP frames drive it.
    };

    /// @brief Constructor.
    explicit LedController() = default;

    /// @brief Destructor.
    ~LedController() = default;

    LedController(const LedController&) = delete;
    LedController& operator=(const LedController&) = delete;

    /// @brief Register the strip with FastLED and paint the initial state.
    /// @note Call from setup(). This used to happen in the constructor, which for
    ///       a file-scope instance meant configuring pins before the Arduino core
    ///       had finished starting.
    void begin();

    /// @brief Choose who owns the strip.
    /// @param mode The new mode.
    /// @note Switching to Udp flashes the strip blue once, as the standalone LED
    ///       firmware did on the link-up edge.
    void set_mode(Mode mode);

    /// @brief The current mode.
    Mode mode() const { return mode_; }

    /// @brief Stage an incoming UDP colour frame.
    /// @param data Raw packet bytes: a header followed by one 32-bit colour per
    ///        LED, low 24 bits used, BGR order.
    /// @param len Packet length; must be at least config::kLedFrameBytes.
    /// @return true if the frame was staged, false if rejected or dropped.
    /// @note Safe to call from the AsyncUDP task. Does not touch FastLED.
    bool submit_frame(const uint8_t *data, size_t len);

    /// @brief Render whatever is pending. The only caller of FastLED.show().
    /// @note Call from loop(). Writes the strip only when something changed, so
    ///       it is cheap to call at loop rate.
    void poll();

private:

    /// @brief Advance and draw the link-down blink.
    void render_link_status();

    /// @brief Push a buffer of packed colours to the strip.
    void show_colors(const uint32_t *colors);

    /// @brief Fill the strip with one colour and show it.
    void show_solid(uint32_t color);

    /// @brief FastLED frame buffer. Only ever written from the loop task.
    CRGB leds_[config::kNumLeds];

    /// @brief Latest frame handed over by the UDP task.
    uint32_t staged_[config::kNumLeds] = {0};

    /// @brief Guards staged_ and frame_pending_ across the two tasks.
    portMUX_TYPE staging_mux_ = portMUX_INITIALIZER_UNLOCKED;

    /// @brief Whether staged_ holds a frame poll() has not shown yet.
    volatile bool frame_pending_{false};

    /// @brief Who owns the strip.
    Mode mode_{Mode::LinkStatus};

    /// @brief Whether begin() has run.
    bool started_{false};

    /// @brief Pending one-shot blue flash for the transition into Udp mode.
    bool blue_flash_pending_{false};

    /// @brief Forces a repaint of the link-status animation.
    bool link_status_dirty_{true};

    /// @brief millis() of the last blink toggle.
    unsigned long last_toggle_ms_{0};

    /// @brief Current phase of the blink.
    bool is_red_{true};
};

} // namespace led_controller

#endif // LED_CONTROLLER_HPP
