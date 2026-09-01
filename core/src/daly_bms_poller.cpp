#include "daly_bms_poller.hpp"
#include "config.hpp"
#include "logging.hpp"

namespace daly100_bms
{

namespace {

/// @brief Commands issued each cycle, in order.
/// @note 0x94 leads because it reports the cell count, which sizes the 0x95 cell
///       voltage assembly that follows.
constexpr uint8_t kCommands[] = {
    Daly100Bms::STATUS_INFO,                  // 0x94
    Daly100Bms::VOUT_IOUT_SOC,                // 0x90
    Daly100Bms::MIN_MAX_CELL_VOLTAGE,         // 0x91
    Daly100Bms::MIN_MAX_TEMPERATURE,          // 0x92
    Daly100Bms::DISCHARGE_CHARGE_MOS_STATUS,  // 0x93
    Daly100Bms::CELL_VOLTAGES,                // 0x95
    Daly100Bms::CELL_TEMPERATURE,             // 0x96
    Daly100Bms::CELL_BALANCE_STATE,           // 0x97
    Daly100Bms::FAILURE_CODES,                // 0x98
};

constexpr uint8_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

/// @brief Size of one request or response frame.
constexpr size_t kFrameBytes = 13;

} // namespace

DalyBmsPoller::DalyBmsPoller(Daly100Bms &bms, connector::BleClient &link)
: bms_(bms)
, link_(link)
{

}

DalyBmsPoller::~DalyBmsPoller()
{
    if (rx_stream_) {
        vStreamBufferDelete(rx_stream_);
        rx_stream_ = nullptr;
    }
}

bool DalyBmsPoller::begin()
{
    if (rx_stream_) {
        return true;
    }

    // Trigger level 1: the reader polls with a zero timeout, so it only needs to
    // return whatever is available.
    rx_stream_ = xStreamBufferCreate(config::kBleRxStreamBytes, 1);

    if (!rx_stream_) {
        ROVER_LOGLN("Failed to allocate BLE receive buffer");
        return false;
    }

    return true;
}

void DalyBmsPoller::on_ble_rx(const uint8_t *data, size_t length)
{
    if (!rx_stream_ || !data || length == 0) {
        return;
    }

    // Zero timeout: this runs on the NimBLE host task and must not block it. A
    // full buffer means the loop task has fallen far behind, in which case the
    // oldest bytes are the least useful anyway.
    const size_t sent = xStreamBufferSend(rx_stream_, data, length, 0);

    if (sent != length) {
        ROVER_LOGLN("BLE receive buffer full, bytes dropped");
    }
}

void DalyBmsPoller::drain_rx()
{
    if (!rx_stream_) {
        return;
    }

    uint8_t chunk[64];

    for (;;) {
        const size_t received = xStreamBufferReceive(rx_stream_, chunk, sizeof(chunk), 0);

        if (received == 0) {
            break;
        }

        bms_.feed(chunk, received);
    }
}

void DalyBmsPoller::poll()
{
    drain_rx();

    if (!link_.is_connected()) {
        // Restart the cycle so a reconnect begins with 0x94 and re-learns the
        // cell count before cell voltages are assembled.
        cmd_index_ = 0;
        return;
    }

    if ((millis() - last_send_ms_) < config::kBmsCommandIntervalMs) {
        return;
    }

    last_send_ms_ = millis();

    uint8_t request[kFrameBytes];

    bms_.create_request(kCommands[cmd_index_], request);
    link_.write(request, sizeof(request));

    if (++cmd_index_ >= kCommandCount) {
        cmd_index_ = 0;
        cycle_complete_ = true;
    }
}

bool DalyBmsPoller::take_cycle_complete()
{
    const bool complete = cycle_complete_;
    cycle_complete_ = false;
    return complete;
}

} // namespace daly100_bms
