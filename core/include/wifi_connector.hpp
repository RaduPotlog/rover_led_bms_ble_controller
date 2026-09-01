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

    /// @brief Connect to the WiFi network using the stored SSID and password.
    void connect() override;

    /// @brief Connect to the WiFi network using the provided SSID and password.
    void connect(const String& ssid, const String& pass) override;
    
    /// @brief Disconnect from the WiFi network.
    void disconnect() override;
    
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
    static bool is_wifi_connected_;
};

} // namespace connector

#endif // WIFI_CONNECTOR_HPP
