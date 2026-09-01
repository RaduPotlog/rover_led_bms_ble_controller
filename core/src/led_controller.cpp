#include "led_controller.hpp"

namespace led_controller
{

namespace {

constexpr uint32_t kColorRed   = 0x00FF0000UL;
constexpr uint32_t kColorBlue  = 0x000000FFUL;
constexpr uint32_t kColorOff   = 0x00000000UL;

} // namespace

void LedController::begin()
{
    if (started_) {
        return;
    }

    FastLED.addLeds<APA102, config::kLedDataPin, config::kLedClockPin, BGR>(leds_, config::kNumLeds);

    started_ = true;
    last_toggle_ms_ = millis();
    link_status_dirty_ = true;

    poll();
}

void LedController::set_mode(Mode mode)
{
    if (mode == mode_) {
        return;
    }

    mode_ = mode;

    if (mode_ == Mode::Udp) {
        blue_flash_pending_ = true;
        return;
    }

    // Taking the strip back from the UDP receiver: restart the blink from its
    // lit phase so the change is immediately visible.
    is_red_ = true;
    last_toggle_ms_ = millis();
    link_status_dirty_ = true;

    // Anything the UDP task staged before the mode changed is stale now.
    portENTER_CRITICAL(&staging_mux_);
    frame_pending_ = false;
    portEXIT_CRITICAL(&staging_mux_);
}

bool LedController::submit_frame(const uint8_t *data, size_t len)
{
    // Dropped rather than queued: while loop() owns the strip, a frame that
    // arrives now would be stale by the time the mode changed.
    if (mode_ != Mode::Udp) {
        return false;
    }

    if (!data || len < config::kLedFrameBytes) {
        return false;
    }

    // memcpy rather than a cast through uint32_t*: nothing guarantees the packet
    // buffer is aligned for a 32-bit load at the header offset.
    portENTER_CRITICAL(&staging_mux_);
    memcpy(staged_, data + config::kLedFrameHeaderBytes, sizeof(staged_));
    frame_pending_ = true;
    portEXIT_CRITICAL(&staging_mux_);

    return true;
}

void LedController::poll()
{
    if (!started_) {
        return;
    }

    if (mode_ == Mode::LinkStatus) {
        render_link_status();
        return;
    }

    if (blue_flash_pending_) {
        blue_flash_pending_ = false;
        show_solid(kColorBlue);
        return;
    }

    if (!frame_pending_) {
        return;
    }

    // Copy out under the lock, then convert and show outside it, so the critical
    // section is a fixed 96-byte memcpy and never spans the SPI write.
    uint32_t frame[config::kNumLeds];

    portENTER_CRITICAL(&staging_mux_);
    memcpy(frame, staged_, sizeof(frame));
    frame_pending_ = false;
    portEXIT_CRITICAL(&staging_mux_);

    show_colors(frame);
}

void LedController::render_link_status()
{
    const unsigned long now = millis();

    if ((now - last_toggle_ms_) >= config::kLedBlinkIntervalMs) {
        is_red_ = !is_red_;
        last_toggle_ms_ = now;
        link_status_dirty_ = true;
    }

    // loop() now runs at kHz rather than roughly once a second, so repainting
    // unconditionally would drive the strip continuously for no visible gain.
    if (!link_status_dirty_) {
        return;
    }

    link_status_dirty_ = false;

    show_solid(is_red_ ? kColorRed : kColorOff);
}

void LedController::show_colors(const uint32_t *colors)
{
    for (int i = 0; i < config::kNumLeds; ++i) {
        leds_[i] = CRGB(colors[i] & 0x00FFFFFFUL);
    }

    FastLED.show();
}

void LedController::show_solid(uint32_t color)
{
    const CRGB c = CRGB(color & 0x00FFFFFFUL);

    for (int i = 0; i < config::kNumLeds; ++i) {
        leds_[i] = c;
    }

    FastLED.show();
}

} // namespace led_controller
