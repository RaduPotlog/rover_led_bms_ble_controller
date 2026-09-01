#ifndef CONNECTOR_INTERFACE_HPP
#define CONNECTOR_INTERFACE_HPP

#include <WString.h>
#include <WiFi.h>

namespace connector
{

/// @brief Interface for network connections.
class ConnectorInterface
{

public:

    /// @brief Connect to the network.
    virtual void connect() = 0;
    
    /// @brief Connect to the network with specific parameters.
    /// @param ssid 
    /// @param pass 
    virtual void connect(const String& ssid, const String& pass) = 0;

    /// @brief Disconnect from the network.
    virtual void disconnect() = 0;
    
    /// @brief Check if the interface connection is active.
    /// @return true if connected, false otherwise.
    virtual bool is_connected() = 0;
};

} // namespace connector

#endif // CONNECTOR_INTERFACE_HPP
