#ifndef BLE_SCANNER_HPP
#define BLE_SCANNER_HPP

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace connector
{

/// @brief Address and signal details of a BLE device seen while scanning.
struct BleDeviceInfo
{
    NimBLEAddress address;  ///< BLE device address
    String name;            ///< BLE device name, empty if not advertised
    int rssi{0};            ///< Received Signal Strength Indicator
    bool found{false};      ///< Whether the target device has been seen
};

/// @brief Scans for the configured BMS and remembers where it was seen.
///
/// @note Named for what it does. It was previously called BleAdvertisedDevice,
///       which described the callback interface it derives from rather than its
///       role, and it owned the scan lifecycle regardless.
class BleScanner : public NimBLEAdvertisedDeviceCallbacks
{

public:

    /// @brief Callback invoked by NimBLE for each advertisement seen.
    /// @param dev The advertising device.
    /// @note Stops the scan once the configured BMS address matches.
    void onResult(NimBLEAdvertisedDevice *dev) override;

    /// @brief Begin scanning, if not already scanning.
    void start();

    /// @brief Stop scanning, if scanning.
    void stop();

    /// @brief Discard the remembered device so a later start() looks again.
    /// @note Needed because "found" previously latched forever: once an
    ///       advertisement had been seen, the address was retried indefinitely
    ///       even after the BMS was powered off or replaced.
    void forget();

    /// @brief Whether the configured BMS has been located.
    bool is_device_found() const { return info_.found; }

    /// @brief Whether a scan is currently running.
    bool is_scanning() const { return scanning_; }

    /// @brief Details of the located device. Only meaningful when found.
    BleDeviceInfo get_device_info() const { return info_; }

private:

    /// @brief Whether a scan is currently running.
    bool scanning_{false};

    /// @brief What was learned about the target device.
    /// @note The NimBLEAdvertisedDevice pointer the previous version also kept
    ///       was never read, and would have dangled: NimBLE owns scan results and
    ///       frees them when the scan is cleared.
    BleDeviceInfo info_{};
};

} // namespace connector

#endif // BLE_SCANNER_HPP
