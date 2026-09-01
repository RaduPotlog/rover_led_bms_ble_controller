#ifndef BLE_CONNECTION_HPP
#define BLE_CONNECTION_HPP

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "connector_interface.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace connector
{


/// @brief BLE device information structure to hold the address, name, RSSI, and found status of the device.
struct BleDeviceInfo {
    NimBLEAddress address;  /// BLE device address
    String name;            /// BLE device name
    int rssi;               /// Received Signal Strength Indicator
    bool found;             /// Status indicating if the device was found
};

// Forward declaration of the BleDevice class to be used in the BleConnection class.
class BleAdvertisedDevice : public NimBLEAdvertisedDeviceCallbacks
{

public:

    /// @brief Callback function that is called when a new BLE device is found during scanning.
    /// @param dev 
    /// @note This function checks if the found device matches the target BMS MAC address. If it does, it updates the BLE device information and stops the scan. It also prints the address and RSSI of each found device to the serial console.
    void onResult(NimBLEAdvertisedDevice* dev) override 
    {
        String addr = dev->getAddress().toString().c_str();
        addr.toLowerCase();
        
        if (addr == config::kBmsMacAddress) {
            bleInfo_.address = dev->getAddress();
            bleInfo_.name = dev->haveName() ? dev->getName().c_str() : "";
            bleInfo_.rssi = dev->getRSSI();
            bleInfo_.found = true;
            nimble_device_ = dev;
            
            if (scanning_) {
                NimBLEDevice::getScan()->stop();
                scanning_ = false;
            }

            ROVER_LOGF("Device was found: %s (RSSI: %d)\n", addr.c_str(), dev->getRSSI());
            return;
        }

        ROVER_LOGF("Found device: %s (RSSI: %d)\n", addr.c_str(), dev->getRSSI());
    }
    
    /// @brief Start scanning for BLE devices.
    /// @note This function initializes the BLE scan parameters and starts the scan. It also sets the callback for handling found devices. If the scan starts successfully, it updates the scanning status and prints a message to the serial console. 
    void scan() 
    {
        if (scanning_) {
            return;
        }

        bleInfo_.found = false;
        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(this);
        scan->setActiveScan(true);
        scan->setInterval(80);
        scan->setWindow(60);
        scan->setMaxResults(0);
        
        if (scan->start(0, [](NimBLEScanResults results) {
                ROVER_LOGLN("Scan complete");
            }, false)) {
            scanning_ = true;
            ROVER_LOGLN("Scan started successfully");
        } else {
            scanning_ = false;
            ROVER_LOGLN("Failed to start scan");
        }
    }

    void stop_scan()
    {
        if (scanning_) {
            NimBLEDevice::getScan()->stop();
            scanning_ = false;
        }
    }

    /// @brief Check if the target BLE device was found during scanning.
    /// @return true if the device was found, false otherwise.
    bool is_device_found() const
    {
        return bleInfo_.found;
    }

    BleDeviceInfo get_ble_advertised_device_info() const
    {
        return bleInfo_;
    }

private:

    /// @brief Flag indicating whether the BLE scan is currently active.
    bool scanning_{false};

    /// @brief Struct to hold information about the found BLE device, including its address, name, RSSI, and whether it was found.
    BleDeviceInfo bleInfo_;

    /// @brief Pointer to the NimBLEAdvertisedDevice object representing the found BLE device. This is updated when the target device is found during scanning. It can be used to access additional information about the device or to initiate a connection.
    NimBLEAdvertisedDevice *nimble_device_{nullptr};
};


/// @brief Concrete implementation of ConnectorInterface for BLE connections.
class BleConnection
{

public:
    
    /// @brief Get the singleton instance of ModbusConnectionWifi.
    /// @return Reference to the singleton instance.
    static BleConnection& getInstance() 
    {
        static BleConnection instance;
        return instance;
    }

    /// @brief Destructor for ConnectionWifi.
    virtual ~BleConnection() = default;

    /// @brief Connect to the BLE network using the provided SSID and password.
    void connect(const String& ssid, const String& pass) 
    {
        (void)pass;
        NimBLEDevice::init(ssid.c_str());
        device_.scan();
    }
    
    /// @brief Disconnect from the BLE network.
    void disconnect() 
    {
        device_.stop_scan();
    }

    /// @brief 
    /// @return 
    BleAdvertisedDevice * get_ble_advertirsed_device() 
    {
        if (device_.is_device_found()) {
            return &device_;
        }

        return nullptr;    
    }
    
    /// @brief Check if the BLE connection is active.
    /// @return true if connected to the BLE network, false otherwise.
    bool is_connected() 
    {
        return device_.is_device_found();
    }

private:

    /// @brief Private constructor for the singleton pattern.
    BleConnection() = default;
    
    /// @brief Deleted copy constructor to prevent copying of the singleton instance.
    BleConnection(const BleConnection&) = delete;
    
    /// @brief Deleted assignment operator to prevent copying of the singleton instance.
    BleConnection& operator=(const BleConnection&) = delete;

    /// @brief Callbacks for when a BLE device is advertised during scanning.
    BleAdvertisedDevice device_;
};

/// @brief BLE client callbacks class to handle connection events and notifications from the BLE device.
class BleClient : public NimBLEClientCallbacks 
{
    
public:
    
    typedef std::function<void (uint8_t* pData, size_t length)> rx_callback;

    /// @brief Callback function that is called after the BLE client successfully connects to the server.
    /// @param client 
    /// @note This function updates the connection status flags and sets up the notification callback for the client. It also prints a message to the serial console indicating that the device has been connected.
    void onConnect(NimBLEClient *client) override
    {
        ROVER_LOGLN("Connected to the BLE device");
    }

    /// @brief Callback function that is called when the BLE client is disconnected from the server.
    /// @param client 
    /// @note This function updates the connection status flags and resets the client and characteristic pointers. It also prints a message to the serial console indicating that the device has been disconnected.
    void onDisconnect(NimBLEClient *client) override 
    {
        if (client_) {
            NimBLEDevice::deleteClient(client_);
            client_ = nullptr;
        }
        
        ROVER_LOGLN("Disconnected from the BLE device");
    }

    /// @brief 
    /// @param connector_interface 
    /// @return 
    bool init(BleConnection *connector_interface, rx_callback callback)
    {
        if (!connector_interface) {
            return false;
        }

        connector_interface_ = connector_interface;
        rx_callback_ = callback;

        return true;
    }

    /// @brief Notification callback
    /// @param pRemoteCharacteristic Pointer to the remote characteristic that triggered the notification.
    /// @param pData Pointer to the data received in the notification.
    /// @param length Length of the data received.
    /// @param isNotify Flag indicating whether the callback was triggered by a notification (true) or an indication (false).
    /// @note This is a placeholder implementation. You should replace the body of this function with your actual logic for processing incoming notifications.
    static void notifyCB(
        NimBLERemoteCharacteristic *pRemoteCharacteristic,
        uint8_t *pData, 
        size_t length, 
        bool isNotify)
    {
        if (rx_callback_) {
            rx_callback_(pData, length);
        }
    }

    /// @brief Check if the BLE client is currently connected to the BMS.
    /// @return true if the BLE client is connected to the BMS, false otherwise.
    bool is_connected() const
    {
        if (!client_) {
            return false;
        }

        return client_->isConnected();
    }

    /// @brief 
    /// @param pkt 
    /// @param lenght 
    /// @return 
    int write(const uint8_t *pkt, const uint8_t lenght)
    {
        if (!client_) {
            return -1;
        }

        if (!rx_characteristic_) {
            return -1;
        }

        if (rx_characteristic_->canWrite()) {
            if (!rx_characteristic_->writeValue(pkt, lenght, true)) {
                return 0;
            }
        } else {
            return 0;
        }

        return lenght;
    }

    /// @brief 
    void pool() 
    {
        if (client_ && client_->isConnected()) {
            return;
        }

        if (!connector_interface_) {
            return;
        }

        BleAdvertisedDevice *dev = connector_interface_->get_ble_advertirsed_device();

        if (!dev) {
            return;
        }
        
        if (client_) {
            NimBLEDevice::deleteClient(client_);
            client_ = nullptr;
        }
        
        if (tx_characteristic_) {
            tx_characteristic_ = nullptr;
        }

        if (rx_characteristic_) {
            rx_characteristic_ = nullptr;
        }
        
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(new BleClient());
        client_->setConnectTimeout(5);
        
        if (!client_->connect(dev->get_ble_advertised_device_info().address)) {
            NimBLEDevice::deleteClient(client_);
            client_ = nullptr;
            // connecting_ = false;
            ROVER_LOGLN("Failed to connect to device");
            return;
        }
        
        if (!discoverAndSubscribe()) {
            client_->disconnect();
            NimBLEDevice::deleteClient(client_);
            client_ = nullptr;
            return;
        }
    }

private:
    
    /// @brief Discover services and characteristics, and subscribe to notifications from the BLE device.
    /// @return true if discovery and subscription were successful, false otherwise.
    bool discoverAndSubscribe() 
    {
        if (!client_) {
            return false;
        }

        if (!client_->isConnected()) {
            return false;
        }

        NimBLERemoteService *svc = client_->getService(config::kServiceUuidMain);
    
        if (!svc) {
            return false;
        }

        tx_characteristic_ = svc->getCharacteristic(config::kCharUuidTx);
        rx_characteristic_ = svc->getCharacteristic(config::kCharUuidRx);
        
        if (!tx_characteristic_ || !rx_characteristic_) {
            ROVER_LOGLN("Subscription failed");
            return false;
        }

        if (tx_characteristic_->canNotify()) {
            tx_characteristic_->subscribe(true, notifyCB);
        }
        
        ROVER_LOGLN("Subscription success");

        return true;
    }

    /// @brief 
    static rx_callback rx_callback_;

    /// @brief 
    BleConnection *connector_interface_{nullptr};

    /// @brief NimBLE client for managing BLE connections and interactions.
    NimBLEClient *client_{nullptr};

    /// @brief Remote characteristic for transmitting data to the BMS (NOTIFY).
    NimBLERemoteCharacteristic *tx_characteristic_{nullptr};

    /// @brief Remote characteristic for receiving data from the BMS (WRITE).
    NimBLERemoteCharacteristic *rx_characteristic_{nullptr};
};

} // namespace connector

#endif // BLE_CONNECTION_HPP