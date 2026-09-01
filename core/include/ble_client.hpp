#ifndef BLE_CLIENT_HPP
#define BLE_CLIENT_HPP

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <functional>

#include "ble_scanner.hpp"
#include "connector_interface.hpp"

namespace connector
{

/// @brief BLE link to the Daly BMS: scans, connects, subscribes, and reconnects.
///
/// @note This absorbs what used to be a separate BleConnection singleton, whose
///       only real responsibilities were calling NimBLEDevice::init() and owning
///       the scanner. Folding them in leaves one class that actually knows the
///       link state, so is_connected() can mean "the link is up" rather than
///       "an advertisement was seen at some point", which is what the old
///       BleConnection::is_connected() reported.
class BleClient : public NimBLEClientCallbacks, public ConnectorInterface
{

public:

    /// @brief Handler for bytes notified by the BMS.
    /// @note Invoked on the NimBLE host task, not from loop().
    using rx_callback = std::function<void(uint8_t *data, size_t length)>;

    /// @brief Constructor.
    BleClient() = default;

    /// @brief Destructor.
    ~BleClient() override = default;

    BleClient(const BleClient &) = delete;
    BleClient &operator=(const BleClient &) = delete;

    /// @brief Initialise the BLE stack and start looking for the BMS.
    /// @param device_name Name this device advertises itself under.
    /// @param cb Handler for notified bytes.
    /// @return true on success.
    bool begin(const char *device_name, rx_callback cb);

    /// @brief Advance the connection state machine.
    /// @note Non-blocking. Performs deferred teardown, rescans when the cached
    ///       address stops working, and throttles connection attempts.
    void poll() override;

    /// @brief Whether the BLE link is up right now.
    bool is_connected() override;

    /// @brief Write a packet to the BMS command characteristic.
    /// @param pkt Bytes to write.
    /// @param length Number of bytes.
    /// @return Bytes written, 0 if the write was refused, -1 if not connected.
    int write(const uint8_t *pkt, size_t length);

    /// @brief NimBLE callback: the link came up.
    void onConnect(NimBLEClient *client) override;

    /// @brief NimBLE callback: the link went down.
    /// @note Runs on the NimBLE host task. It only raises a flag -- deleting the
    ///       client here would destroy the object this callback is running
    ///       against, which NimBLE does not support.
    void onDisconnect(NimBLEClient *client) override;

private:

    /// @brief Locate the BMS service and subscribe to its notify characteristic.
    bool discover_and_subscribe();

    /// @brief Release the NimBLE client and the characteristics that point into it.
    void teardown_client();

    /// @brief Scans for, and remembers, the configured BMS.
    BleScanner scanner_;

    /// @brief Handler for notified bytes.
    /// @note A plain member. It was previously static, with its storage defined
    ///       over in main.cpp, which meant this header could not be used by any
    ///       other translation unit.
    rx_callback rx_callback_{};

    /// @brief NimBLE client for managing BLE connections and interactions.
    NimBLEClient *client_{nullptr};

    /// @brief Remote characteristic the BMS notifies on (BMS -> host).
    NimBLERemoteCharacteristic *tx_characteristic_{nullptr};

    /// @brief Remote characteristic commands are written to (host -> BMS).
    NimBLERemoteCharacteristic *rx_characteristic_{nullptr};

    /// @brief Set by onDisconnect on the NimBLE task, consumed by poll().
    volatile bool pending_cleanup_{false};

    /// @brief millis() of the last connection attempt.
    unsigned long last_attempt_ms_{0};

    /// @brief Consecutive failed connects against the cached address.
    uint8_t failed_connects_{0};
};

} // namespace connector

#endif // BLE_CLIENT_HPP
