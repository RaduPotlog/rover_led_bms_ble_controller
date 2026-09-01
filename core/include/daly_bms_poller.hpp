#ifndef DALY_BMS_POLLER_HPP
#define DALY_BMS_POLLER_HPP

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#include "ble_client.hpp"
#include "daly_100_bms.hpp"

namespace daly100_bms
{

/// @brief Drives the BMS request/response conversation without blocking.
///
/// @note Two problems are solved here that used to live in main.cpp.
///
///       First, timing. The command burst was nine writes each followed by
///       delay(100), so loop() blocked for most of a second and every other
///       subsystem inherited that cadence. This issues one command per poll()
///       once the interval has elapsed and returns immediately; the cycle still
///       takes about 900 ms, but nothing waits on it.
///
///       Second, threading. Notified bytes were decoded straight from the NimBLE
///       host task into the shared BmsData that loop() was concurrently reading
///       and serialising. Here on_ble_rx() only pushes bytes into a stream buffer
///       and poll() drains it, so the decoded state has a single reader/writer:
///       the loop task.
class DalyBmsPoller
{

public:

    /// @brief Constructor.
    /// @param bms Protocol decoder to feed and to build requests from.
    /// @param link BLE link used to write commands.
    DalyBmsPoller(Daly100Bms &bms, connector::BleClient &link);

    /// @brief Destructor.
    ~DalyBmsPoller();

    DalyBmsPoller(const DalyBmsPoller &) = delete;
    DalyBmsPoller &operator=(const DalyBmsPoller &) = delete;

    /// @brief Allocate the receive stream buffer.
    /// @return true on success.
    bool begin();

    /// @brief Hand over bytes notified by the BMS.
    /// @param data Notified bytes.
    /// @param length Number of bytes.
    /// @note Called on the NimBLE host task. Does no decoding and never blocks;
    ///       bytes are dropped if the buffer is full.
    void on_ble_rx(const uint8_t *data, size_t length);

    /// @brief Drain received bytes and issue the next command when due.
    /// @note Call from loop().
    void poll();

    /// @brief Consume the "a full command cycle finished" edge.
    /// @return true once after each complete pass over the command list.
    /// @note This is the cue to publish telemetry, replacing "once per blocking
    ///       loop iteration".
    bool take_cycle_complete();

private:

    /// @brief Move buffered bytes into the decoder.
    void drain_rx();

    /// @brief Protocol decoder.
    Daly100Bms &bms_;

    /// @brief BLE link used to write commands.
    connector::BleClient &link_;

    /// @brief Bytes from the NimBLE task awaiting decode on the loop task.
    StreamBufferHandle_t rx_stream_{nullptr};

    /// @brief millis() of the last command written.
    unsigned long last_send_ms_{0};

    /// @brief Index of the next command to send.
    uint8_t cmd_index_{0};

    /// @brief Set when the command list wraps, cleared by take_cycle_complete().
    bool cycle_complete_{false};
};

} // namespace daly100_bms

#endif // DALY_BMS_POLLER_HPP
