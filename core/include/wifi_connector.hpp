#ifndef WIFI_CONNECTOR_HPP
#define WIFI_CONNECTOR_HPP

#include <Arduino.h>
#include <WiFi.h>

#include "connector_interface.hpp"

namespace connector
{

/// @brief Concrete implementation of ConnectorInterface for WiFi connections.
class WifiConnector : public ConnectorInterface
{

public:
    
    /// @brief Get the singleton instance of WifiConnector.
    /// @return Reference to the singleton instance.
    static WifiConnector& getInstance();

    /// @brief Destructor for WifiConnector.
    virtual ~WifiConnector() = default;
    
    /// @brief Assign a fixed address instead of requesting one over DHCP.
    /// @param local Address to claim.
    /// @param gateway Default gateway.
    /// @param subnet Subnet mask.
    /// @param dns1 Primary DNS server.
    /// @param dns2 Secondary DNS server.
    /// @note Must be called before connect(). When it is not called the station
    ///       falls back to DHCP.
    void set_static_ip(
        const IPAddress& local,
        const IPAddress& gateway,
        const IPAddress& subnet,
        const IPAddress& dns1,
        const IPAddress& dns2);

    /// @brief Join the WiFi network using the provided credentials.
    /// @param ssid Network name.
    /// @param pass Pre-shared key.
    /// @note Not part of ConnectorInterface -- credentials are specific to WiFi.
    ///       Call once from setup(); poll() maintains the link from then on.
    void connect(const String& ssid, const String& pass);

    /// @brief Disconnect from the WiFi network.
    void disconnect();

    /// @brief Retry the connection if it has been down for kWifiReconnectMs.
    /// @note The reconnect used to be issued straight from the disconnect event
    ///       handler, which hammered a down AP as fast as it could refuse.
    void poll() override;

    /// @brief Check if the WiFi connection is active.
    /// @return true if connected to the WiFi network, false otherwise.
    bool is_connected() override;

private:

    /// @brief Private constructor for the singleton pattern.
    WifiConnector() = default;
    
    /// @brief Deleted copy constructor to prevent copying of the singleton instance.
    WifiConnector(const WifiConnector&) = delete;
    
    /// @brief Deleted assignment operator to prevent copying of the singleton instance.
    WifiConnector& operator=(const WifiConnector&) = delete;
    
    /// @brief Callback for when the station is connected to the WiFi network.
    /// @param event 
    /// @param info 
    static void station_connected_callback(WiFiEvent_t event, WiFiEventInfo_t info);

    /// @brief Callback for when the station gets an IP address after connecting to the WiFi network.
    /// @param event 
    /// @param info 
    static void got_ip_callback(WiFiEvent_t event, WiFiEventInfo_t info);

    /// @brief Callback for when the station is disconnected from the WiFi network.
    /// @param event 
    /// @param info 
    static void disconnected_callback(WiFiEvent_t event, WiFiEventInfo_t info);

    /// @brief Print the current WiFi status, including SSID, IP address, and signal strength.
    static void print_status();

    /// @brief WiFi network credentials.
    String ssid_{""};

    /// @brief WiFi network password.
    String pass_{""};

    /// @brief Whether set_static_ip() supplied a fixed address configuration.
    bool use_static_ip_{false};

    /// @brief Fixed address configuration, valid only when use_static_ip_ is set.
    IPAddress static_ip_;
    IPAddress gateway_;
    IPAddress subnet_;
    IPAddress dns1_;
    IPAddress dns2_;

    /// @brief Connection state, shared with the static event callbacks.
    /// @note Static because the Arduino WiFi event API takes a plain function
    ///       pointer. Safe here only because this class is a singleton.
    static bool is_wifi_connected_;

    /// @brief millis() of the last reconnect attempt.
    unsigned long last_reconnect_ms_{0};
};

} // namespace connector

#endif // WIFI_CONNECTOR_HPP
