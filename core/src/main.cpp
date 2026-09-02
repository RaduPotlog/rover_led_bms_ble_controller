#include <Arduino.h>

#include "EasyNextionLibrary.h"

#include "config.hpp"
#include "logging.hpp"
#include "wifi_connector.hpp"
#include "udp_connection.hpp"
#include "ble_client.hpp"
#include "daly_100_bms.hpp"
#include "daly_bms_poller.hpp"
#include "bms_telemetry_codec.hpp"
#include "led_controller.hpp"
#include "nextion_view.hpp"

/* ==========================================================================
 * Globals
 * ==========================================================================
 * One WiFi link and one BLE link, shared by two independent UDP sockets:
 * gBmsUdp pushes serialized BMS telemetry out, gLedUdp receives colour frames
 * for the strip. UdpConnection holds a per-instance AsyncUDP, so the two ports
 * coexist without further plumbing.
 *
 * Three tasks touch these objects: the Arduino loop task, the NimBLE host task
 * (BLE notifications) and the async_udp task (incoming packets). Only two
 * handoffs cross a task boundary and both are explicit -- DalyBmsPoller's stream
 * buffer and LedController's staging buffer. Everything else belongs to the loop
 * task, which is what lets the decoded BMS state be read here without a lock.
 */

static connector::WifiConnector &gWifi = connector::WifiConnector::getInstance();
static connector::BleClient gBleClient;
static connector::UdpConnection gBmsUdp;
static connector::UdpConnection gLedUdp;

static daly100_bms::Daly100Bms gDaly;
static daly100_bms::DalyBmsPoller gPoller(gDaly, gBleClient);

static led_controller::LedController gLeds;

static EasyNex gNexDisplay(Serial);
static ui::NextionView gView(gNexDisplay, Serial);

/// @brief Outbound telemetry payload.
static uint8_t gTelemetryBuf[telemetry::BmsTelemetryCodec::kPacketSize];

/* ==========================================================================
 * Arduino entry points
 * ========================================================================== */

void setup()
{
    Serial.begin(config::kSerialBaud);

    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);
    
    ROVER_LOG_BEGIN();
    ROVER_LOGLN("Rover controller starting (BMS BLE + Nextion + UDP + LED)...");

    gLeds.begin();
    gView.begin();

#if ROVER_WIFI_USE_STATIC_IP
    gWifi.set_static_ip(config::static_ip(), config::gateway(), config::subnet(),
                        config::dns1(), config::dns2());
#endif

    gWifi.connect(config::kWifiSsid, config::kWifiPass);

    gBmsUdp.init(&gWifi, config::kBmsUdpPort, [](AsyncUDPPacket packet) {
        (void)packet; // telemetry is one-way; nothing is expected on this socket
    });
    gBmsUdp.set_destination(config::bms_udp_dest_ip(), config::kBmsUdpDestPort);

    gLedUdp.init(&gWifi, config::kLedUdpPort, [](AsyncUDPPacket packet) {
        // Runs on the async_udp task: stage the bytes and return. The strip is
        // written only from loop(), via gLeds.poll().
        gLeds.submit_frame(packet.data(), packet.length());
    });

    gPoller.begin();

    gBleClient.begin(config::kBleDeviceName, [](uint8_t *data, size_t length) {
        // Runs on the NimBLE host task: hand the bytes over, decode on the loop.
        gPoller.on_ble_rx(data, length);
    });
}

void loop()
{
    /* --- links ---------------------------------------------------------- */

    gWifi.poll();
    gBleClient.poll();

    const bool wifi_up = gWifi.is_connected();
    const bool ble_up = gBleClient.is_connected();

    /* --- sockets -------------------------------------------------------- */

    gBmsUdp.poll();
    gLedUdp.poll();

    /* --- LED strip ------------------------------------------------------ */

    // While the link is down loop() shows the status blink; once it is up the
    // UDP receiver's frames take over. LedController enforces the handover, so
    // there is never more than one writer.
    gLeds.set_mode(wifi_up ? led_controller::LedController::Mode::Udp
                           : led_controller::LedController::Mode::LinkStatus);
    gLeds.poll();

    /* --- BMS ------------------------------------------------------------ */

    gPoller.poll();

    if (gPoller.take_cycle_complete()) {
        size_t length = 0;

        if (ble_up) {
            length = telemetry::BmsTelemetryCodec::serialize(
                gDaly.get_data(), gDaly.get_alarm(), gTelemetryBuf, sizeof(gTelemetryBuf));
        }

        if (length == 0) {
            // Keep the packet cadence steady when the BMS is unreachable, but send
            // an unambiguously empty payload rather than stale readings.
            memset(gTelemetryBuf, 0, sizeof(gTelemetryBuf));
            length = sizeof(gTelemetryBuf);
        }

        gBmsUdp.send(gTelemetryBuf, length);
    }

    /* --- display -------------------------------------------------------- */

    gView.update(*gDaly.get_data(), wifi_up, ble_up);
    gView.poll();
}
