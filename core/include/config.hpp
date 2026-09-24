#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <Arduino.h>
#include <IPAddress.h>

/// @brief Deployment configuration for the rover LED controller firmware.
/// @note This is the single place to edit when moving the board between networks.
///
/// @warning These are deliberately typed constants rather than #defines. FastLED
///          uses DATA_PIN and CLOCK_PIN as its own template parameter names, so
///          macros by those names corrupt its headers wherever this file is
///          included first.

/// @brief Set to 1 to assign a static IP instead of using DHCP.
/// @note When 0 (the default) the station takes its address from the router's DHCP server.
#ifndef ROVER_WIFI_USE_STATIC_IP
#define ROVER_WIFI_USE_STATIC_IP 0
#endif

namespace config
{

/* --------------------------------------------------------------------------
 * Serial
 * -------------------------------------------------------------------------- */

/// @brief Baud rate of UART0, which carries the debug console alone.
/// @note Must match monitor_speed in platformio.ini. This was 9600 while a
///       Nextion HMI shared the port at the rate its Editor project was built
///       for; with no display on the line there is nothing to hold it down.
constexpr unsigned long kSerialBaud = 115200UL;

/* --------------------------------------------------------------------------
 * WiFi
 * -------------------------------------------------------------------------- */

/* Credentials live in config_local.hpp, which is git-ignored. Copy
 * config_local.hpp.example next to it and fill in your network.
 *
 * NOTE: the previous passphrase was committed in the clear and is still in this
 * repository's history. Moving it out of the tracked file stops it spreading
 * further but does not retract it -- rotate the AP passphrase to remediate. */
#if defined(__has_include)
#  if __has_include("config_local.hpp")
#    include "config_local.hpp"
#  endif
#endif

#ifndef ROVER_WIFI_SSID
#define ROVER_WIFI_SSID "ROVER-A1-001-2.4GHz"
#endif

#ifndef ROVER_WIFI_PASS
#define ROVER_WIFI_PASS "set-me-in-config_local.hpp"
#endif

/// @brief SSID of the network the controller joins.
constexpr const char *kWifiSsid = ROVER_WIFI_SSID;

/// @brief Pre-shared key for kWifiSsid.
constexpr const char *kWifiPass = ROVER_WIFI_PASS;

/// @brief Gap between WiFi reconnection attempts, in milliseconds.
/// @note The disconnect event handler used to call WiFi.reconnect() directly,
///       which retried as fast as the AP could refuse while it was down.
constexpr unsigned long kWifiReconnectMs = 5000UL;

/* IPAddress has a constructor, so a `static const IPAddress` at namespace scope in
 * a header gives every translation unit its own dynamically-initialised copy and
 * makes the initialisation order across TUs unspecified. Inline functions have
 * neither problem and cost nothing -- the value is constructed where it is used. */

#if ROVER_WIFI_USE_STATIC_IP
/// @brief Static address to claim when ROVER_WIFI_USE_STATIC_IP is enabled.
inline IPAddress static_ip()  { return IPAddress(192, 168, 77, 201); }
/// @brief Default gateway used with static_ip().
inline IPAddress gateway()    { return IPAddress(192, 168, 77, 1); }
/// @brief Subnet mask used with static_ip().
inline IPAddress subnet()     { return IPAddress(255, 255, 255, 0); }
/// @brief Primary DNS server used with static_ip().
inline IPAddress dns1()       { return IPAddress(8, 8, 8, 8); }
/// @brief Secondary DNS server used with static_ip().
inline IPAddress dns2()       { return IPAddress(8, 8, 4, 4); }
#endif // ROVER_WIFI_USE_STATIC_IP

/* --------------------------------------------------------------------------
 * UDP
 * --------------------------------------------------------------------------
 * One socket, receive-only. It must sit on the same subnet as the host running
 * rover_led, which is the network kWifiSsid names.
 */

/// @brief Port the LED socket listens on for incoming colour frames.
constexpr int kLedUdpPort = 3333;

/* --------------------------------------------------------------------------
 * LED strip
 * -------------------------------------------------------------------------- */

/// @brief Number of LEDs on the strip.
/// @note Override with -D ROVER_NUM_LEDS=<n> in platformio.ini rather than
///       editing this file. Everything else about the strip -- both frame
///       buffers, every render loop and the UDP frame size -- derives from it.
#ifndef ROVER_NUM_LEDS
#define ROVER_NUM_LEDS 40
#endif

constexpr int kNumLeds = ROVER_NUM_LEDS;
static_assert(kNumLeds > 0, "ROVER_NUM_LEDS must be positive");

/// @brief Strip data line.
constexpr uint8_t kLedDataPin = 5;

/// @brief Strip clock line. Unused when ROVER_LED_CLOCKLESS is 1.
constexpr uint8_t kLedClockPin = 16;

/// @brief SPI clock rate for a clocked chipset, in MHz.
/// @note 6 is FastLED's own default for APA102 and the reason is in its source:
///       "APA102 has a bug where long strip can't handle full speed due to clock
///       degredation" (chipsets.h, APA102Controller's SPI_SPEED parameter). It is
///       stated here rather than left implicit so that changing it is one build
///       flag instead of an argument buried in a template default.
/// @note Not a literal frequency. DATA_RATE_MHZ(X) expands to a cycles-per-bit
///       divider, (F_CPU / 1000000) / X, and the achieved rate runs below nominal
///       because the GPIO writes themselves are not counted.
#ifndef ROVER_LED_SPI_MHZ
#define ROVER_LED_SPI_MHZ 6
#endif
constexpr uint32_t kLedSpiMhz = ROVER_LED_SPI_MHZ;

/// @brief Global FastLED brightness, 0-255.
/// @note This is the knob that separates a power limit from a data limit: if the
///       whole strip lights at 32 but only the first few at 255, the supply is
///       browning out and needs 5 V injected at the far end.
#ifndef ROVER_LED_BRIGHTNESS
#define ROVER_LED_BRIGHTNESS 255
#endif
constexpr uint8_t kLedBrightness = ROVER_LED_BRIGHTNESS;

/// @brief Current budget for the strip at 5 V, in milliamps. 0 disables the cap.
/// @note FastLED scales brightness down to stay inside this. It is the honest way
///       to run a strip on a supply that cannot feed it -- 40 APA102s at full
///       white want around 2.4 A, which a dev board's 5 V pin will not give.
/// @note Left at 0 by default deliberately. A cap that silently dims the strip
///       would hide the very wiring fault it is compensating for; reach for it
///       only once the supply has been measured and cannot be changed.
#ifndef ROVER_LED_MAX_MILLIAMPS
#define ROVER_LED_MAX_MILLIAMPS 0
#endif
constexpr uint32_t kLedMaxMilliamps = ROVER_LED_MAX_MILLIAMPS;

/// @brief Half-period of the red link-down blink, in milliseconds.
constexpr unsigned long kLedBlinkIntervalMs = 2000UL;

/// @brief How long the strip may go without an incoming UDP frame before it
///        drops back to the solid blue "link up, no LED data" state, in
///        milliseconds.
/// @note rover_led sends continuously at its controller_frequency (50 Hz by
///       default), even when the animation is unchanged, so 1 s is about fifty
///       missed frames -- the sender has stopped or the link is dead. Without
///       this the strip held the last frame for as long as the WiFi stack still
///       believed it was associated, which may be forever.
constexpr unsigned long kLedFrameTimeoutMs = 1000UL;

/// @brief Bytes of header preceding the colour data in an incoming LED frame.
constexpr size_t kLedFrameHeaderBytes = 4;

/// @brief A full-size incoming LED frame: header plus one 32-bit colour per LED,
///        low 24 bits used, BGR order.
/// @note Not a minimum. Shorter frames are accepted and paint as far as they
///       reach; see LedController::submit_frame().
constexpr size_t kLedFrameBytes =
    kLedFrameHeaderBytes + (static_cast<size_t>(kNumLeds) * sizeof(uint32_t));

/// @brief Shortest frame that still carries one colour, and so the length below
///        which an incoming packet is rejected outright.
constexpr size_t kLedMinFrameBytes = kLedFrameHeaderBytes + sizeof(uint32_t);

} // namespace config

#endif // CONFIG_HPP
