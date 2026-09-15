#include "led_controller.hpp"

#include "logging.hpp"

namespace led_controller
{

namespace {

constexpr uint32_t kColorRed   = 0x00FF0000UL;
constexpr uint32_t kColorBlue  = 0x000000FFUL;
constexpr uint32_t kColorWhite = 0x00FFFFFFUL;
constexpr uint32_t kColorOff   = 0x00000000UL;

#if ROVER_LED_SELFTEST
/// @brief Dwell on each LED of the self-test chase, in milliseconds.
constexpr unsigned long kSelfTestStepMs = 40UL;

/// @brief How long the self-test holds the dim fill, in milliseconds.
constexpr unsigned long kSelfTestHoldMs = 1500UL;

/// @brief Gap between frames in the ROVER_LED_SELFTEST=2 scope hold, in
///        milliseconds. Long enough to separate frames on a scope, short enough
///        that a slow sweep still catches one.
constexpr unsigned long kSelfTestHoldStepMs = 20UL;

/// @brief Brightness of the self-test's dim fill.
/// @note Low enough that the whole strip lit at once draws a fraction of what
///       it would at full brightness, which is the point of the phase.
constexpr uint8_t kSelfTestDimBrightness = 24;
#endif

} // namespace

void LedController::begin()
{
    if (started_) {
        return;
    }

#if ROVER_LED_CLOCKLESS
    FastLED.addLeds<ROVER_LED_CHIPSET, config::kLedDataPin, ROVER_LED_COLOR_ORDER>(
        leds_, config::kNumLeds);
#else
    // The clock is bit-banged whatever pins this uses: FASTLED_ALL_PINS_HARDWARE_SPI
    // is never defined in this build, so on ESP32 FastLED's SPIOutput resolves to
    // the software implementation for every pin. The rate is therefore worth
    // stating explicitly -- it is a real variable on a long strip, and a template
    // default is a bad place to hide one.
    FastLED.addLeds<ROVER_LED_CHIPSET, config::kLedDataPin, config::kLedClockPin,
                    ROVER_LED_COLOR_ORDER, DATA_RATE_MHZ(config::kLedSpiMhz)>(
        leds_, config::kNumLeds);
#endif

    FastLED.setBrightness(config::kLedBrightness);

#if ROVER_LED_MAX_MILLIAMPS
    // Trades brightness for staying inside what the supply can actually deliver,
    // rather than letting the far end of the strip brown out.
    FastLED.setMaxPowerInVoltsAndMilliamps(5, config::kLedMaxMilliamps);
#endif

    started_ = true;

#if ROVER_LED_SELFTEST
    run_self_test();
#endif

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
        showing_idle_ = false;

        // Start the frame timeout from the link-up edge, not from whatever frame
        // was last seen before the link dropped.
        portENTER_CRITICAL(&staging_mux_);
        last_frame_ms_ = millis();
        portEXIT_CRITICAL(&staging_mux_);
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

    if (!data || len < config::kLedMinFrameBytes) {
        return false;
    }

    // Paint as much of the strip as the frame actually covers rather than
    // insisting on an exact length. A sender that has not been updated for the
    // current ROVER_NUM_LEDS would otherwise have every frame rejected here, and
    // the strip would sit frozen on its last colour with nothing to say why.
    const size_t carried = (len - config::kLedFrameHeaderBytes) / sizeof(uint32_t);
    const size_t used = (carried < static_cast<size_t>(config::kNumLeds))
                            ? carried
                            : static_cast<size_t>(config::kNumLeds);

    // memcpy rather than a cast through uint32_t*: nothing guarantees the packet
    // buffer is aligned for a 32-bit load at the header offset.
    portENTER_CRITICAL(&staging_mux_);
    memcpy(staged_, data + config::kLedFrameHeaderBytes, used * sizeof(uint32_t));
    // A short frame leaves a dark tail rather than whatever the last full frame
    // put there, so what reaches the strip is always the frame that arrived.
    memset(staged_ + used, 0, (static_cast<size_t>(config::kNumLeds) - used) * sizeof(uint32_t));
    frame_pending_ = true;
    last_frame_ms_ = millis();
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
        // The link-up blue is also the no-data state, so it simply stays until
        // the first frame arrives.
        blue_flash_pending_ = false;
        showing_idle_ = true;
        show_solid(kColorBlue);
        return;
    }

    // Copy out under the lock, then convert and show outside it, so the critical
    // section is a fixed-size memcpy of the frame buffer and never spans the SPI
    // write.
    uint32_t frame[config::kNumLeds];
    bool have_frame = false;
    unsigned long last_frame_ms = 0;

    portENTER_CRITICAL(&staging_mux_);
    if (frame_pending_) {
        memcpy(frame, staged_, sizeof(frame));
        frame_pending_ = false;
        have_frame = true;
    }
    last_frame_ms = last_frame_ms_;
    portEXIT_CRITICAL(&staging_mux_);

    if (have_frame) {
        showing_idle_ = false;
        show_colors(frame);
        return;
    }

    // No frame for a while: the sender stopped or the link is dead even though
    // WiFi still reports connected. Go back to the no-data blue rather than
    // holding the last frame indefinitely.
    if (!showing_idle_ && (millis() - last_frame_ms) >= config::kLedFrameTimeoutMs) {
        ROVER_LOGLN("LED frames timed out, showing no-data state");
        showing_idle_ = true;
        show_solid(kColorBlue);
    }
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

#if ROVER_LED_SELFTEST
void LedController::run_self_test()
{
    ROVER_LOGF("LED self-test: %d LEDs on data pin %u\n",
               config::kNumLeds, static_cast<unsigned>(config::kLedDataPin));
#if ROVER_LED_CLOCKLESS
    ROVER_LOGLN("LED self-test: single-wire chipset, clock pin unused");
#else
    ROVER_LOGF("LED self-test: clocked chipset, clock pin %u at %u MHz\n",
               static_cast<unsigned>(config::kLedClockPin),
               static_cast<unsigned>(config::kLedSpiMhz));
#endif

#if ROVER_LED_SELFTEST == 2
    /* Scope hold.
     *
     * Nothing to watch on the strip -- this exists for a probe. The same frame
     * goes out over and over on a fixed cadence, so the bitstream is periodic and
     * a scope can trigger on it cleanly at any pixel's CI or DI pads. Dim,
     * because the point is to look at edges rather than to load the supply.
     *
     * This never returns: the board does not reach loop(), so no WiFi, no BLE and
     * no UDP frames compete for the strip while probing. */
    ROVER_LOGLN("LED self-test: scope hold, repeating one frame; loop() is not reached");

    FastLED.setBrightness(kSelfTestDimBrightness);

    for (;;) {
        show_solid(kColorWhite);
        delay(kSelfTestHoldStepMs);
    }
#endif

    /* Phase 1 -- chase.
     *
     * One LED lit at a time, so the strip draws roughly a single pixel's current
     * however long it is. That takes the supply out of the picture: whatever this
     * phase shows is the data path. If the dot walks the whole strip, data and
     * clock reach the far end and any missing LEDs are a power problem. If it
     * stops short, the bitstream is not arriving past that index -- wrong
     * chipset for the strip, a broken line, or too fast a clock. */
    for (int i = 0; i < config::kNumLeds; ++i) {
        for (int j = 0; j < config::kNumLeds; ++j) {
            leds_[j] = CRGB(kColorOff);
        }

        leds_[i] = CRGB(kColorWhite);
        FastLED.show();
        delay(kSelfTestStepMs);
    }

    /* Phase 2 -- dim fill.
     *
     * The whole strip at once, but dim enough that the total current stays a
     * fraction of what full brightness would pull. A strip that lights end to end
     * here and only part way at full brightness is browning out and wants 5 V
     * injected at the far end. */
    FastLED.setBrightness(kSelfTestDimBrightness);
    show_solid(kColorWhite);
    delay(kSelfTestHoldMs);

    /* Phase 3 -- restore. */
    FastLED.setBrightness(config::kLedBrightness);
    show_solid(kColorOff);

    ROVER_LOGLN("LED self-test: done");
}
#endif // ROVER_LED_SELFTEST

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
