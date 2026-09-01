#include "udp_connection.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace connector
{

UdpConnection::UdpConnection()
: connector_interface_{nullptr}
, port_(0)
{

}

bool UdpConnection::init(
    ConnectorInterface *connector_interface,
    const uint16_t port,
    AuPacketHandlerFunction cb)
{
    if (!connector_interface) {
        return false;
    }

    connector_interface_ = connector_interface;
    port_ = port;
    data_recv_handler_ = cb;
    is_initialized_ = true;

    return true;
}

void UdpConnection::set_destination(const IPAddress &ip, const uint16_t port)
{
    dest_ip_ = ip;
    dest_port_ = port;
    has_destination_ = true;
}

bool UdpConnection::poll()
{
    if (is_initialized_ == false) {
        return false;
    }

    if (!connector_interface_) {
        return false;
    }

    if (!connector_interface_->is_connected()) {
        if (udp_.connected()) {
            udp_.close();
            ROVER_LOGLN("UDP connection closing...");
        }

        return (is_interface_connected_ = false);
    }

    if (is_interface_connected_ == false) {
        ROVER_LOGLN("Start udp listener...");

        if (udp_.listen(port_)) {
            udp_.onPacket([this](AsyncUDPPacket packet) {
                if (data_recv_handler_) {
                    data_recv_handler_(packet);
                }
            });

            ROVER_LOGLN("UDP listening on port " + String(port_) + "...");
            is_interface_connected_ = true;
        } else {
            ROVER_LOGLN("Failed to start UDP listener");
            return (is_interface_connected_ = false);
        }
    }

    return true;
}

size_t UdpConnection::send(const uint8_t *data, const size_t len)
{
    if (is_initialized_ == false || !data || len == 0) {
        return 0;
    }

    if (!has_destination_) {
        return 0;
    }

    if (!is_interface_connected_) {
        return 0;
    }

    return udp_.writeTo(data, len, dest_ip_, dest_port_);
}

bool UdpConnection::is_connected()
{
    return is_interface_connected_;
}

} // namespace connector
