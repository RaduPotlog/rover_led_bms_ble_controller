#ifndef CONNECTOR_INTERFACE_HPP
#define CONNECTOR_INTERFACE_HPP

namespace connector
{

/// @brief A network link whose state gates the things layered on top of it.
///
/// @note Deliberately narrow. Bringing a link up is specific to the medium --
///       WiFi needs an SSID and passphrase, BLE needs a device name and a scan --
///       so connect() lives on the concrete classes and is called once from
///       setup(). What callers share is the need to ask whether the link is up
///       and to give it a slice of time, and that is all this interface promises.
///
///       The previous version also declared connect(), connect(ssid, pass) and
///       disconnect(). Only WifiConnector implemented them, the no-argument
///       connect() had an empty body, and BleConnection did not derive from this
///       interface at all despite claiming to in its comment.
class ConnectorInterface
{

public:

    /// @brief Destructor.
    virtual ~ConnectorInterface() = default;

    /// @brief Check if the link is currently usable.
    /// @return true if connected.
    virtual bool is_connected() = 0;

    /// @brief Give the link a slice of time to advance its state.
    /// @note Called every iteration of loop(), so implementations must not block.
    virtual void poll() = 0;
};

} // namespace connector

#endif // CONNECTOR_INTERFACE_HPP
