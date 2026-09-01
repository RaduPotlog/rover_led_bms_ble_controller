#ifndef UDP_CONNECTION_HPP
#define UDP_CONNECTION_HPP

#include <Arduino.h>
#include <AsyncUDP.h>
#include <IPAddress.h>

#include "connector_interface.hpp"

namespace connector
{

/// @brief A UDP socket bound to one port, layered on top of a ConnectorInterface.
///
/// @note Instantiate one per port. The combined firmware runs two: one for BMS
///       telemetry out and one for incoming LED colour frames.
///
///       The send destination is supplied by the owner via set_destination().
///       It used to be read straight out of config::kBmsUdpDestIp, which meant
///       this general-purpose class knew about the BMS, and the LED instance
///       carried a destination it could never sensibly use.
class UdpConnection
{

public:

    /// @brief Constructor.
    explicit UdpConnection();

    /// @brief Deleted copy constructor.
    UdpConnection(const UdpConnection&) = delete;

    /// @brief Deleted assignment operator.
    UdpConnection& operator=(const UdpConnection&) = delete;

    /// @brief Destructor for UdpConnection.
    virtual ~UdpConnection() =  default;

    /// @brief Bind this socket to a port and register a receive handler.
    /// @param connector_interface The underlying link whose state gates the socket.
    /// @param port Port to listen on.
    /// @param cb Handler invoked from the AsyncUDP task for each received packet.
    /// @return true on success.
    bool init(ConnectorInterface *connector_interface, const uint16_t port, AuPacketHandlerFunction cb);

    /// @brief Set where send() delivers to.
    /// @param ip Destination host.
    /// @param port Destination port.
    /// @note Optional. A socket that only receives never needs one, and send()
    ///       refuses until it has been called.
    void set_destination(const IPAddress &ip, const uint16_t port);

    /// @brief Send a datagram to the configured destination.
    /// @param data Payload.
    /// @param len Payload length in bytes.
    /// @return Bytes written, or 0 if the socket was not ready or has no destination.
    size_t send(const uint8_t *data, const size_t len);

    /// @brief Bring the listener up or down to follow the underlying link state.
    /// @return true while the socket is listening.
    bool poll();

    /// @brief Check if the UDP connection is active.
    /// @return true if the listener is up, false otherwise.
    bool is_connected();

private:

    /// @brief Callback for when a UDP packet is received.
    AuPacketHandlerFunction data_recv_handler_;

    /// @brief The underlying link whose state gates this socket.
    ConnectorInterface *connector_interface_;

    /// @brief AsyncUDP instance for managing the UDP connection.
    AsyncUDP udp_;

    /// @brief Destination for send(), valid only when has_destination_ is set.
    IPAddress dest_ip_;

    /// @brief Destination port for send().
    uint16_t dest_port_{0};

    /// @brief UDP port. Always supplied by init().
    uint16_t port_{0};

    /// @brief Whether set_destination() has been called.
    bool has_destination_{false};

    /// @brief Whether init() has run.
    bool is_initialized_{false};

    /// @brief Whether the listener is currently up.
    bool is_interface_connected_{false};
};

} // namespace connector

#endif // UDP_CONNECTION_HPP
