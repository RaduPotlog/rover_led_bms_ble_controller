#include "ble_client.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace connector
{

bool BleClient::begin(const char *device_name, rx_callback cb)
{
    if (!device_name) {
        return false;
    }

    rx_callback_ = cb;

    NimBLEDevice::init(device_name);
    scanner_.start();

    return true;
}

void BleClient::onConnect(NimBLEClient *client)
{
    (void)client;
    ROVER_LOGLN("Connected to the BLE device");
}

void BleClient::onDisconnect(NimBLEClient *client)
{
    (void)client;

    // Deliberately does no cleanup: NimBLE calls this from its host task while
    // the client is still in use, so deleting it here would free the object the
    // stack is about to touch. poll() picks this up on the next iteration.
    pending_cleanup_ = true;

    ROVER_LOGLN("Disconnected from the BLE device");
}

bool BleClient::is_connected()
{
    return client_ && client_->isConnected();
}

void BleClient::teardown_client()
{
    // The characteristics belong to the client's service objects, so they are
    // dangling the moment the client goes away. Clear them first.
    tx_characteristic_ = nullptr;
    rx_characteristic_ = nullptr;

    if (client_) {
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
    }
}

void BleClient::poll()
{
    if (pending_cleanup_) {
        pending_cleanup_ = false;
        teardown_client();
    }

    if (is_connected()) {
        failed_connects_ = 0;
        return;
    }

    // A client object that exists but is not connected is left over from a failed
    // attempt or a drop that has already been flagged; clear it before retrying.
    if (client_) {
        teardown_client();
    }

    if (!scanner_.is_device_found()) {
        scanner_.start(); // no-op while a scan is already running
        return;
    }

    if ((millis() - last_attempt_ms_) < config::kBleConnectRetryMs) {
        return;
    }

    last_attempt_ms_ = millis();

    client_ = NimBLEDevice::createClient();

    if (!client_) {
        ROVER_LOGLN("Failed to allocate BLE client");
        return;
    }

    // false: this object outlives the client, so NimBLE must not delete it.
    // Passing a freshly allocated callbacks object here, as the previous version
    // did, leaked one per attempt and delivered onDisconnect to an instance that
    // held no client pointer.
    client_->setClientCallbacks(this, false);
    client_->setConnectTimeout(5);

    if (!client_->connect(scanner_.get_device_info().address)) {
        teardown_client();

        ROVER_LOGLN("Failed to connect to device");

        if (++failed_connects_ >= config::kBleMaxFailedConnects) {
            // The cached address has stopped working. Forget it and look again
            // rather than retrying the same unreachable device forever.
            ROVER_LOGLN("Giving up on cached BMS address, rescanning");
            failed_connects_ = 0;
            scanner_.forget();
        }

        return;
    }

    if (!discover_and_subscribe()) {
        client_->disconnect();
        teardown_client();
        return;
    }

    failed_connects_ = 0;
}

bool BleClient::discover_and_subscribe()
{
    if (!client_ || !client_->isConnected()) {
        return false;
    }

    NimBLERemoteService *svc = client_->getService(config::kServiceUuidMain);

    if (!svc) {
        ROVER_LOGLN("BMS service not found");
        return false;
    }

    tx_characteristic_ = svc->getCharacteristic(config::kCharUuidTx);
    rx_characteristic_ = svc->getCharacteristic(config::kCharUuidRx);

    if (!tx_characteristic_ || !rx_characteristic_) {
        ROVER_LOGLN("Subscription failed");
        return false;
    }

    if (tx_characteristic_->canNotify()) {
        // NimBLE types notify_callback as a std::function, so this can capture
        // `this` and reach a per-instance handler. That is what lets rx_callback_
        // be an ordinary member instead of a static whose storage lived in main.
        const bool ok = tx_characteristic_->subscribe(
            true,
            [this](NimBLERemoteCharacteristic *chr, uint8_t *data, size_t length, bool is_notify) {
                (void)chr;
                (void)is_notify;

                if (rx_callback_) {
                    rx_callback_(data, length);
                }
            });

        if (!ok) {
            ROVER_LOGLN("Subscribe rejected");
            return false;
        }
    }

    ROVER_LOGLN("Subscription success");

    return true;
}

int BleClient::write(const uint8_t *pkt, size_t length)
{
    if (!is_connected() || !rx_characteristic_) {
        return -1;
    }

    if (!rx_characteristic_->canWrite()) {
        return 0;
    }

    if (!rx_characteristic_->writeValue(pkt, length, true)) {
        return 0;
    }

    return static_cast<int>(length);
}

} // namespace connector
