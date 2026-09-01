#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <Arduino.h>
#include <IPAddress.h>

/// @brief Deployment configuration for the combined rover controller firmware.
/// @note This is the single place to edit when moving the board between networks.
///       Everything here was previously hardcoded across main.cpp, udp_connection.cpp,
///       wifi_connector.cpp, ble_connection.hpp and led_controller.hpp.
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

/// @brief SSID of the network the controller joins.
constexpr const char *kWifiSsid = "ROVER-A1-001-2.4GHz";

/// @brief Pre-shared key for kWifiSsid.
/// @note Stored in the clear, as it was in both original projects. If this repo
///       ever becomes public, move this into an ignored config_local.hpp.
constexpr const char *kWifiPass = "Madrid280287!";

#if ROVER_WIFI_USE_STATIC_IP
/// @brief Static address to claim when ROVER_WIFI_USE_STATIC_IP is enabled.
static const IPAddress kStaticIp(192, 168, 99, 101);
/// @brief Default gateway used with kStaticIp.
static const IPAddress kGateway(192, 168, 99, 1);
/// @brief Subnet mask used with kStaticIp.
static const IPAddress kSubnet(255, 255, 255, 0);
/// @brief Primary DNS server used with kStaticIp.
static const IPAddress kDns1(8, 8, 8, 8);
/// @brief Secondary DNS server used with kStaticIp.
static const IPAddress kDns2(8, 8, 4, 4);
#endif // ROVER_WIFI_USE_STATIC_IP

/* --------------------------------------------------------------------------
 * UDP
 * --------------------------------------------------------------------------
 * WARNING: as inherited, these three addresses sit on mutually unreachable
 * subnets -- the BMS telemetry target is on 192.168.1.0/24, the LED controller's
 * static address was on 192.168.99.0/24, and the WiFi network above hands out a
 * third range over DHCP. On one board they must agree. Fix kBmsUdpDestIp (and
 * kStaticIp, if enabled) to match the network in kWifiSsid before deploying.
 */

/// @brief Port the BMS telemetry socket binds to and sends from.
constexpr int kBmsUdpPort = 4444;

/// @brief Host that receives the serialized BMS data and alarm payload.
static const IPAddress kBmsUdpDestIp(192, 168, 1, 201);

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

/* --------------------------------------------------------------------------
 * LED strip
 * -------------------------------------------------------------------------- */

/// @brief Number of APA102 LEDs on the strip.
constexpr int kNumLeds = 24;

/// @brief APA102 data line.
constexpr uint8_t kLedDataPin = 13;

/// @brief APA102 clock line.
constexpr uint8_t kLedClockPin = 14;

/// @brief Half-period of the red link-down blink, in milliseconds.
constexpr unsigned long kLedBlinkIntervalMs = 2000UL;

} // namespace config

#endif // CONFIG_HPP
