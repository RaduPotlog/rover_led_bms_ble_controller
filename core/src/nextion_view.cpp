#include "nextion_view.hpp"

namespace ui
{

namespace {

/* Picture indices baked into the .HMI project in resources/. */
constexpr int kWifiIconUp     = 3;
constexpr int kWifiIconDown   = 2;
constexpr int kBleIconUp      = 1;
constexpr int kBleIconDown    = 2;
constexpr int kMosfetIconOn   = 5;
constexpr int kMosfetIconOff  = 4;

/// @brief Text for the charge/discharge status byte reported by 0x93.
const char *charge_state_text(int state)
{
    switch (state) {
        case 0:  return "Stationary";
        case 1:  return "Charging";
        case 2:  return "Discharging";
        default: return "Unknown";
    }
}

} // namespace

NextionView::NextionView(EasyNex &display, HardwareSerial &port)
: display_(display)
, port_(port)
, text_{
    { "state.txt",       "", "", true },
    { "voltage_val.txt", "", "", true },
    { "current_val.txt", "", "", true },
    { "temp_val.txt",    "", "", true },
    { "soc_val.txt",     "", "", true },
  }
, num_{
    { "wifi_icon.pic",      0, -1, true },
    { "ble_icon.pic",       0, -1, true },
    { "chg_mos_state.pic",  0, -1, true },
    { "dchg_mos_state.pic", 0, -1, true },
  }
{

}

void NextionView::begin()
{
    display_.writeStr("page main");

    started_ = true;
}

void NextionView::set_text(TextField &field, const char *value)
{
    if (strncmp(field.desired, value, kMaxTextLen) == 0) {
        return;
    }

    strncpy(field.desired, value, kMaxTextLen - 1);
    field.desired[kMaxTextLen - 1] = '\0';

    field.dirty = (strncmp(field.desired, field.shown, kMaxTextLen) != 0);
}

void NextionView::set_num(NumField &field, int value)
{
    field.desired = value;
    field.dirty = (field.desired != field.shown);
}

void NextionView::update(
    const daly100_bms::Daly100Bms::BmsData &data,
    bool wifi_up,
    bool ble_up)
{
    char buf[kMaxTextLen];

    set_text(text_[0], charge_state_text(data.chargeDischargeStatus));

    snprintf(buf, sizeof(buf), "%.2f V", data.packVoltage);
    set_text(text_[1], buf);

    snprintf(buf, sizeof(buf), "%.2f A", data.packCurrent);
    set_text(text_[2], buf);

    snprintf(buf, sizeof(buf), "%.2f", data.tempMax);
    set_text(text_[3], buf);

    snprintf(buf, sizeof(buf), "%.2f", data.packSOC);
    set_text(text_[4], buf);

    set_num(num_[0], wifi_up ? kWifiIconUp : kWifiIconDown);
    set_num(num_[1], ble_up ? kBleIconUp : kBleIconDown);
    set_num(num_[2], data.chargeFetState ? kMosfetIconOn : kMosfetIconOff);
    set_num(num_[3], data.disChargeFetState ? kMosfetIconOn : kMosfetIconOff);
}

void NextionView::poll()
{
    if (!started_) {
        return;
    }

    constexpr size_t kTextCount = sizeof(text_) / sizeof(text_[0]);
    constexpr size_t kNumCount = sizeof(num_) / sizeof(num_[0]);
    constexpr size_t kFieldCount = kTextCount + kNumCount;

    for (size_t attempt = 0; attempt < kFieldCount; ++attempt) {

        const size_t idx = cursor_;
        cursor_ = (cursor_ + 1) % kFieldCount;

        const bool is_text = (idx < kTextCount);

        if (is_text && !text_[idx].dirty) {
            continue;
        }

        if (!is_text && !num_[idx - kTextCount].dirty) {
            continue;
        }

        // Writing into a full transmit buffer would block until the UART drained,
        // which at 9600 baud is exactly the stall this class exists to avoid.
        if (port_.availableForWrite() < static_cast<int>(config::kNextionMinTxFree)) {
            return;
        }

        if (is_text) {
            TextField &field = text_[idx];

            display_.writeStr(field.name, field.desired);

            strncpy(field.shown, field.desired, kMaxTextLen - 1);
            field.shown[kMaxTextLen - 1] = '\0';
            field.dirty = false;
        } else {
            NumField &field = num_[idx - kTextCount];

            display_.writeNum(field.name, field.desired);

            field.shown = field.desired;
            field.dirty = false;
        }

        // One field per call, so a burst of changes is spread across iterations
        // instead of monopolising the UART.
        return;
    }
}

} // namespace ui
