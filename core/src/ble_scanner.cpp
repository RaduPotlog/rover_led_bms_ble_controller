#include "ble_scanner.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace connector
{

void BleScanner::onResult(NimBLEAdvertisedDevice *dev)
{
    if (!dev) {
        return;
    }

    String addr = dev->getAddress().toString().c_str();
    addr.toLowerCase();

    if (addr != config::kBmsMacAddress) {
        ROVER_LOGF("Found device: %s (RSSI: %d)\n", addr.c_str(), dev->getRSSI());
        return;
    }

    info_.address = dev->getAddress();
    info_.name = dev->haveName() ? dev->getName().c_str() : "";
    info_.rssi = dev->getRSSI();
    info_.found = true;

    if (scanning_) {
        NimBLEDevice::getScan()->stop();
        scanning_ = false;
    }

    ROVER_LOGF("Device was found: %s (RSSI: %d)\n", addr.c_str(), dev->getRSSI());
}

void BleScanner::start()
{
    if (scanning_) {
        return;
    }

    info_.found = false;

    NimBLEScan *scan = NimBLEDevice::getScan();

    scan->setAdvertisedDeviceCallbacks(this);
    scan->setActiveScan(true);
    scan->setInterval(80);
    scan->setWindow(60);
    scan->setMaxResults(0);

    // Duration 0 scans until stopped, which onResult does on a match.
    if (scan->start(0, [](NimBLEScanResults results) { (void)results; }, false)) {
        scanning_ = true;
        ROVER_LOGLN("Scan started successfully");
    } else {
        scanning_ = false;
        ROVER_LOGLN("Failed to start scan");
    }
}

void BleScanner::stop()
{
    if (!scanning_) {
        return;
    }

    NimBLEDevice::getScan()->stop();
    scanning_ = false;
}

void BleScanner::forget()
{
    stop();

    // Drop cached results too, so the next scan reports the device only if it is
    // genuinely advertising again.
    NimBLEDevice::getScan()->clearResults();

    info_.found = false;
    info_.rssi = 0;
    info_.name = "";
}

} // namespace connector
