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
    const int port,
    AuPacketHandlerFunction cb)
{
    connector_interface_ = connector_interface;
    port_ = port;
    is_initialized_ = true;
    data_recv_handler_ = cb;

    return true;
}

bool UdpConnection::pool() 
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
            ROVER_LOGLN("UDP listening on port " + String(port_) + "...");
            
            udp_.onPacket([this](AsyncUDPPacket packet) {
                if (data_recv_handler_) {
                    data_recv_handler_(packet);
                }
            });

            ROVER_LOGLN("UDP listening on port " + String(port_) + " started successfully!");
            is_interface_connected_ = true;
        } else {
            ROVER_LOGLN("Failed to start UDP listener");
            return (is_interface_connected_ = false);
        }
    }

    return true;
}

bool UdpConnection::send(const uint8_t *data, const size_t len) 
{
    if (is_initialized_ == false) {
        return false;
    }

    if (!is_interface_connected_) {
        ROVER_LOGLN("UDP connection is not active. Cannot send data.");
        return false;
    }
    
    size_t bytes_sent = udp_.writeTo(data, len, config::kBmsUdpDestIp, port_);
    
    (void)bytes_sent; // Suppress unused variable warning
    
    return true;
}

bool UdpConnection::is_connected() 
{
    return is_interface_connected_;
}

} // namespace connector
