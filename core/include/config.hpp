#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <Arduino.h>
#include <IPAddress.h>

/// @brief Deployment configuration for the combined rover controller firmware.
/// @note This is the single place to edit when moving the board between networks.
///       Everything here was previously hardcoded across main.cpp, udp_connection.cpp,
///       wifi_connector.cpp, the BLE sources and led_controller.hpp.
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
 * Serial / Nextion display
 * --------------------------------------------------------------------------
 * The Nextion HMI shares UART0 with the USB serial console, so this baud rate
 * applies to both. It must match monitor_speed in platformio.ini and the baud
 * rate configured in the Nextion Editor project.
 */

/// @brief Baud rate of UART0, shared by the Nextion display and the debug console.
constexpr unsigned long kSerialBaud = 9600UL;

/* --------------------------------------------------------------------------
 * WiFi
 * --------------------------------------------------------------------------
 * NOTE: the two source projects used different networks -- the BMS controller
 * joined "ROVER-A1-001-2.4GHz" over DHCP while the LED controller took a static
 * 192.168.99.101/24. A single firmware can only join one network, so the BMS
 * network is the default here. See the UDP block below for the matching subnet
 * caveat.
 */

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
inline IPAddress static_ip()  { return IPAddress(192, 168, 99, 101); }
/// @brief Default gateway used with static_ip().
inline IPAddress gateway()    { return IPAddress(192, 168, 99, 1); }
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
 * WARNING: as inherited, these three addresses sit on mutually unreachable
 * subnets -- the BMS telemetry target is on 192.168.1.0/24, the LED controller's
 * static address was on 192.168.99.0/24, and the WiFi network above hands out a
 * third range over DHCP. On one board they must agree. Fix bms_udp_dest_ip() (and
 * static_ip(), if enabled) to match the network in kWifiSsid before deploying.
 * Until they do, telemetry leaves the board and is dropped by the first router.
 */

/// @brief Port the BMS telemetry socket binds to and sends from.
constexpr int kBmsUdpPort = 4444;

/// @brief Host that receives the serialized BMS data and alarm payload.
inline IPAddress bms_udp_dest_ip() { return IPAddress(192, 168, 1, 201); }

/// @brief Port on bms_udp_dest_ip() that telemetry is sent to.
/// @note Kept equal to kBmsUdpPort, which is what the original firmware did by
///       sending from and to the same port number.
constexpr int kBmsUdpDestPort = 4444;

/// @brief Port the LED socket listens on for incoming colour frames.
constexpr int kLedUdpPort = 3333;

/* --------------------------------------------------------------------------
 * BLE / Daly BMS
 * -------------------------------------------------------------------------- */

/// @brief Name this device advertises itself under when initialising NimBLE.
constexpr const char *kBleDeviceName = "daly100-ble-controller";

/// @brief MAC address of the target Daly BMS. Update to match your pack.
/// @note Compared lowercased, so keep it lowercase.
constexpr const char *kBmsMacAddress = "50:19:02:01:32:3e";

/// @brief UUID of the BMS main service.
constexpr const char *kServiceUuidMain = "0000fff0-0000-1000-8000-00805f9b34fb";

/// @brief UUID of the characteristic the BMS notifies on (BMS -> host).
constexpr const char *kCharUuidTx = "0000fff1-0000-1000-8000-00805f9b34fb";

/// @brief UUID of the characteristic commands are written to (host -> BMS).
constexpr const char *kCharUuidRx = "0000fff2-0000-1000-8000-00805f9b34fb";

/// @brief Gap between consecutive BMS commands, in milliseconds.
/// @note The nine-command cycle therefore takes about 900 ms, matching the
///       original firmware's cadence -- but as a deadline rather than a delay().
constexpr unsigned long kBmsCommandIntervalMs = 100UL;

/// @brief Minimum gap between BLE connection attempts, in milliseconds.
/// @note Without this the now non-blocking loop would retry thousands of times a
///       second while the BMS is unreachable.
constexpr unsigned long kBleConnectRetryMs = 2000UL;

/// @brief Capacity of the buffer holding notified BLE bytes until loop() decodes
///        them, in bytes.
/// @note Sized for well over a full nine-command cycle of 13-byte responses, so a
///       momentarily slow loop() iteration cannot lose frames.
constexpr size_t kBleRxStreamBytes = 512;

/// @brief Consecutive failed connects before the cached address is discarded.
/// @note Guards against retrying an address forever after the BMS has gone away
///       or come back with a different one.
constexpr uint8_t kBleMaxFailedConnects = 3;

/* --------------------------------------------------------------------------
 * LED strip
 * -------------------------------------------------------------------------- */

/// @brief Number of APA102 LEDs on the strip.
constexpr int kNumLeds = 24;

/// @brief APA102 data line.
constexpr uint8_t kLedDataPin = 5;

/// @brief APA102 clock line.
constexpr uint8_t kLedClockPin = 16;

/// @brief Half-period of the red link-down blink, in milliseconds.
constexpr unsigned long kLedBlinkIntervalMs = 2000UL;

/// @brief Bytes of header preceding the colour data in an incoming LED frame.
constexpr size_t kLedFrameHeaderBytes = 4;

/// @brief Smallest acceptable incoming LED frame: header plus one 32-bit colour
///        per LED, low 24 bits used, BGR order.
constexpr size_t kLedFrameBytes = kLedFrameHeaderBytes + (kNumLeds * sizeof(uint32_t));

/* --------------------------------------------------------------------------
 * Nextion display
 * -------------------------------------------------------------------------- */

/// @brief Free space required in the UART TX buffer before writing a field.
/// @note At 9600 baud a field update is around 30 bytes and takes ~30 ms to
///       clock out. Checking for room instead of calling delay() is what lets
///       loop() stay non-blocking.
constexpr size_t kNextionMinTxFree = 64;

} // namespace config

#endif // CONFIG_HPP
