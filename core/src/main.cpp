#include <Arduino.h>

#include "config.hpp"
#include "logging.hpp"
#include "wifi_connector.hpp"
#include "udp_connection.hpp"
#include "led_controller.hpp"

/* ==========================================================================
 * Globals
 * ==========================================================================
 * One WiFi link and one UDP socket: gLedUdp receives colour frames for the
 * strip. The socket only ever receives, so it is never given a destination.
 *
 * Two tasks touch these objects: the Arduino loop task and the async_udp task
 * (incoming packets). Exactly one handoff crosses a task boundary and it is
 * explicit -- LedController's staging buffer. Everything else belongs to the
 * loop task.
 */

static connector::WifiConnector &gWifi = connector::WifiConnector::getInstance();
static connector::UdpConnection gLedUdp;

static led_controller::LedController gLeds;

/* ==========================================================================
 * Arduino entry points
 * ========================================================================== */

void setup()
{
    Serial.begin(config::kSerialBaud);

    // Onboard LED, held high as a crude "the board is powered and running" sign.
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);

    ROVER_LOG_BEGIN();
    ROVER_LOGLN("Rover LED controller starting (WiFi + UDP + LED)...");

    gLeds.begin();

#if ROVER_WIFI_USE_STATIC_IP
    gWifi.set_static_ip(config::static_ip(), config::gateway(), config::subnet(),
                        config::dns1(), config::dns2());
#endif

    gWifi.connect(config::kWifiSsid, config::kWifiPass);

    gLedUdp.init(&gWifi, config::kLedUdpPort, [](AsyncUDPPacket packet) {
        // Runs on the async_udp task: stage the bytes and return. The strip is
        // written only from loop(), via gLeds.poll().
        gLeds.submit_frame(packet.data(), packet.length());
    });
}

void loop()
{
    /* --- link ----------------------------------------------------------- */

    gWifi.poll();

    const bool wifi_up = gWifi.is_connected();

    /* --- socket --------------------------------------------------------- */

    gLedUdp.poll();

    /* --- LED strip ------------------------------------------------------ */

    // While the link is down loop() shows the status blink; once it is up the
    // UDP receiver's frames take over. LedController enforces the handover, so
    // there is never more than one writer.
    gLeds.set_mode(wifi_up ? led_controller::LedController::Mode::Udp
                           : led_controller::LedController::Mode::LinkStatus);
    gLeds.poll();
}
