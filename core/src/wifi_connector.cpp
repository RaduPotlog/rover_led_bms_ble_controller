#include "wifi_connector.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace connector
{

bool WifiConnector::is_wifi_connected_ = false;

WifiConnector& WifiConnector::getInstance()
{
    static WifiConnector instance;
    return instance;
}

void WifiConnector::station_connected_callback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    ROVER_LOGLN("Connected to router successfully!");
}

void WifiConnector::got_ip_callback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    ROVER_LOGLN("WiFi connected");
    ROVER_LOGLN("IP address: ");
    ROVER_LOGLN(WiFi.localIP());

    is_wifi_connected_ = true;
}

void WifiConnector::disconnected_callback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    ROVER_LOGLN("Disconnected from WiFi access point");
    ROVER_LOG("WiFi lost connection. Reason: ");
    ROVER_LOGLN(info.wifi_sta_disconnected.reason);
    ROVER_LOGLN("Trying to Reconnect");

    // Reconnecting from here would retry immediately and repeatedly for as long
    // as the AP stays down. poll() does it on a timer instead.
    is_wifi_connected_ = false;
}

void WifiConnector::print_status() 
{
    ROVER_LOG("SSID: ");
    ROVER_LOGLN(WiFi.SSID());

    IPAddress ip = WiFi.localIP();
    ROVER_LOG("IP Address: ");
    ROVER_LOGLN(ip);

    long rssi = WiFi.RSSI();
    ROVER_LOG("signal strength (RSSI):");
    ROVER_LOG(rssi);
    ROVER_LOGLN(" dBm");
}

void WifiConnector::set_static_ip(
    const IPAddress& local,
    const IPAddress& gateway,
    const IPAddress& subnet,
    const IPAddress& dns1,
    const IPAddress& dns2)
{
    static_ip_ = local;
    gateway_ = gateway;
    subnet_ = subnet;
    dns1_ = dns1;
    dns2_ = dns2;
    use_static_ip_ = true;
}

void WifiConnector::connect(
    const String& ssid, 
    const String& pass)
{
    ssid_ = ssid;
    pass_ = pass;

    WiFi.onEvent(station_connected_callback, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(got_ip_callback, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(disconnected_callback, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);

    // Printed here rather than on the got-IP event so that it appears even when
    // the AP is unreachable -- this is the address a DHCP reservation is keyed
    // on, and it is needed most when the board is not associating.
    ROVER_LOG("WiFi MAC: ");
    ROVER_LOGLN(WiFi.macAddress());

    if (use_static_ip_) {
        if (!WiFi.config(static_ip_, gateway_, subnet_, dns1_, dns2_)) {
            ROVER_LOGLN("STA Failed to configure");
            return;
        }
    }

    WiFi.begin(ssid_, pass_);
}

void WifiConnector::disconnect()
{
    WiFi.disconnect();
}

void WifiConnector::poll()
{
    if (is_wifi_connected_) {
        last_reconnect_ms_ = millis();
        return;
    }

    if ((millis() - last_reconnect_ms_) < config::kWifiReconnectMs) {
        return;
    }

    last_reconnect_ms_ = millis();

    ROVER_LOGLN("Retrying WiFi connection...");
    WiFi.reconnect();
}

bool WifiConnector::is_connected()
{   
    return is_wifi_connected_;
}

} // namespace connector
