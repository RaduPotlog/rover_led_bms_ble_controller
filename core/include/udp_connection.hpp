#ifndef UDP_CONNECTION_HPP
#define UDP_CONNECTION_HPP

#include <Arduino.h>
#include <AsyncUDP.h>
#include <IPAddress.h>

#include "connector_interface.hpp"

namespace connector
{

/// @brief A UDP socket bound to one port, layered on top of a ConnectorInterface.
/// @note Instantiate one per port. The combined firmware runs two: one for BMS
///       telemetry out and one for incoming LED colour frames.
class UdpConnection
{

public:
    
    /// @brief Constructor.
    explicit UdpConnection();
    
    /// @brief Deleted copy constructor.
    UdpConnection(const UdpConnection&) = delete;
    
    /// @brief Destructor for UdpConnection.
    virtual ~UdpConnection() =  default;
    
    /// @brief Bind this socket to a port and register a receive handler.
    /// @param connector_interface The underlying link whose state gates the socket.
    /// @param port Port to listen on (and, for send(), to send to).
    /// @param cb Handler invoked from the AsyncUDP task for each received packet.
    /// @return true on success.
    bool init(ConnectorInterface *connector_interface, const int port, AuPacketHandlerFunction cb);

    /// @brief Send a datagram to config::kBmsUdpDestIp on this socket's port.
    /// @param data Payload.
    /// @param len Payload length in bytes.
    /// @return true if the socket was ready and the write was attempted.
    bool send(const uint8_t *data, const size_t len);

    /// @brief Bring the listener up or down to follow the underlying link state.
    /// @return true while the socket is listening.
    bool pool();

    /// @brief Check if the UDP connection is active.
    /// @return true if connected to the UDP endpoint, false otherwise.
    bool is_connected();

private:

    /// @brief Deleted assignment operator.
    UdpConnection& operator=(const UdpConnection&) = delete;

    /// @brief Callback for when a UDP packet is received.
    /// @param packet The received UDP packet.
    AuPacketHandlerFunction data_recv_handler_;

    /// @brief The underlying link whose state gates this socket.
    ConnectorInterface *connector_interface_;

    /// @brief AsyncUDP instance for managing the UDP connection.
    AsyncUDP udp_;

    /// @brief UDP port. Always supplied by init().
    uint16_t port_{0};

    /// @brief Whether init() has run.
    bool is_initialized_{false};

    /// @brief Whether the listener is currently up.
    bool is_interface_connected_{false};
};

} // namespace connector

#endif // UDP_CONNECTION_HPP
