#include "daly_100_bms.hpp"
#include "logging.hpp"

namespace daly100_bms 
{

void Daly100Bms::reset_cell_assembly() 
{
    memset(frame_received_, 0, sizeof(frame_received_));
    frames_received_count_ = 0;
    assembled_ = false;
}

bool Daly100Bms::is_valid_frame(const uint8_t *pkt, size_t len) 
{
    if (!pkt || len < 2) {
        return false;
    }
    
    uint8_t sum = 0;

    for (size_t i = 0; i < len - 1; ++i) {
        sum += pkt[i];
    }

    return (sum == pkt[len - 1]);
}

void Daly100Bms::feed(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }

    // On overflow drop the oldest bytes, not the newest: whatever is at the head
    // is a fragment that never completed, while the arriving bytes may finish a
    // frame. The previous implementation discarded the whole buffer instead.
    if (len >= kRxBufferSize) {
        data += (len - kRxBufferSize);
        len = kRxBufferSize;
        rx_len_ = 0;
    } else if (rx_len_ + len > kRxBufferSize) {
        const size_t drop = (rx_len_ + len) - kRxBufferSize;
        memmove(rx_buf_, rx_buf_ + drop, rx_len_ - drop);
        rx_len_ -= drop;
    }

    memcpy(rx_buf_ + rx_len_, data, len);
    rx_len_ += len;

    // Scan with an index and compact once at the end, rather than memmove-ing the
    // remainder on every byte.
    size_t head = 0;

    while ((rx_len_ - head) >= kFrameSize) {

        if (rx_buf_[head] != 0xA5 || !is_valid_frame(rx_buf_ + head, kFrameSize)) {
            ++head;
            continue;
        }

        decode_response(rx_buf_ + head);
        head += kFrameSize;
    }

    if (head > 0) {
        rx_len_ -= head;
        memmove(rx_buf_, rx_buf_ + head, rx_len_);
    }
}

void Daly100Bms::recompute_expected_frames()
{
    if (expectedCellCount < 1) {
        expectedCellCount = 8;
    }
    
    if (expectedCellCount > 48) {
        expectedCellCount = 48;
    }
    
    expectedFramesNeeded = (expectedCellCount + 3 - 1) / 3; // ceil div3
}

Daly100Bms::ReturnStatus Daly100Bms::get_pack_measurements(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;
        
    // Pull the relevant values out of the buffer
    get.packVoltage = (static_cast<float>((payload[0] << 8) | payload[1])) / 10.0f;
    // The current measurement is given with a 30000 unit offset (see /docs/)
    get.packCurrent = (static_cast<float>((payload[4] << 8) | payload[5]) - 30000) / 10.0f;
    get.packSOC = (static_cast<float>((payload[6] << 8) | payload[7]) / 10.0f);

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_min_max_cell_voltage(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;
    
    get.maxCellmV = (float)((payload[0] << 8) | payload[1]);
    get.maxCellVNum = payload[2];
    get.minCellmV = (float)((payload[3] << 8) | payload[4]);
    get.minCellVNum = payload[5];
    get.cellDiff = (get.maxCellmV - get.minCellmV);

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_pack_temp(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    // An offset of 40 is added by the BMS to avoid having to deal with negative numbers, see protocol in /docs/
    get.tempMax = (payload[0] - 40.0f);
    get.tempMin = (payload[2] - 40.0f);
    get.tempAverage = (get.tempMax + get.tempMin) / 2.0f;

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_discharge_charge_mosfet_status(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    get.chargeDischargeStatus = payload[0];
    get.chargeFetState = payload[1];
    get.disChargeFetState = payload[2];
    get.bmsHeartBeat = payload[3];
    get.resCapacitymAh = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) | \
                         ((uint32_t)payload[6] << 8) | (uint32_t)payload[7];
    
    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_status_info(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    get.numberOfCells = payload[0];
    get.numOfTempSensors = payload[1];
    get.chargeState = payload[2];
    get.loadState = payload[3];

    // Parse the 8 bits into 8 booleans that represent the states of the Digital IO
    for (size_t i = 0; i < 8; i++) {
        get.dIO[i] = bitRead(payload[4], i);
    }

    get.bmsCycles = ((uint16_t)payload[5] << 8) | (uint16_t)payload[6];

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_cell_voltages(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;
    
    uint8_t rawFrameNo = payload[0];

    if (!frame_base_detected_) {
        if (rawFrameNo == 0) {
            frame_base_ = 0;
        } else {
            frame_base_ = 1;
        }
        
        frame_base_detected_ = true;
    }

    int idx = (int)rawFrameNo - frame_base_;
    
    if (idx < 0 || idx >= 16) {
        return E_NOK;
    }
    
    if (assembled_) {
        return E_OK;
    }

    if (frame_received_[idx]) {
        return E_OK;
    }
    
    // Each 0x95 frame carries exactly three cells, and which three is determined
    // by the frame index -- payload[1..6] is three big-endian millivolt pairs.
    for (int i = 0; i < 3; ++i) {
        const int cellNo = (idx * 3) + i;

        if (cellNo >= expectedCellCount || cellNo >= kMaxCells) {
            break;
        }

        get.cellVmV[cellNo] = static_cast<float>((payload[1 + 2 * i] << 8) | payload[2 + 2 * i]);
    }

    frame_received_[idx] = true;
    frames_received_count_++;

    bool allGot = true;

    for (int f = 0; f < expectedFramesNeeded; ++f) {
        if (!frame_received_[f]) {
            allGot = false;
            break;
        }
    }

    if (allGot) {
#if ROVER_DEBUG_BMS
        print_cell_voltages();
#endif
        assembled_ = true;
    }

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_cell_temperature(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    // Like 0x95, each frame carries a fixed slice -- seven sensors here -- and the
    // frame index says which slice. The BMS is inconsistent about whether frame
    // numbering starts at 0 or 1, so reuse the base detected for cell voltages and
    // fall back to treating the first frame seen as the base.
    const int rawFrameNo = payload[0];
    const int base_frame = frame_base_detected_ ? frame_base_ : (rawFrameNo == 0 ? 0 : 1);
    const int idx = rawFrameNo - base_frame;

    if (idx < 0) {
        return E_NOK;
    }

    // The BMS adds a +40 offset so it never has to send a negative number.
    for (int j = 0; j < 7; ++j) {
        const int sensorNo = (idx * 7) + j;

        if (sensorNo >= get.numOfTempSensors || sensorNo >= kMaxTempSensors) {
            break;
        }

        get.cellTemperature[sensorNo] = payload[1 + j] - 40;
    }

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_cell_balance_state(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    int cellBalance = 0;

    // Six bytes, one bit per cell, LSB first -- exactly 48 bits for kMaxCells.
    // These live at payload[0..5]; the previous payload[i + 4] applied the
    // frame-to-payload offset a second time and read past the end of the frame.
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 8; j++) {
            const bool balancing = bitRead(payload[i], j) != 0;

            get.cellBalanceState[(i * 8) + j] = balancing;

            if (balancing) {
                cellBalance++;
            }
        }
    }

    get.cellBalanceActive = (cellBalance > 0);

    return ret;
}

Daly100Bms::ReturnStatus Daly100Bms::get_failure_codes(const uint8_t *payload)
{
    ReturnStatus ret = E_OK;

    /* 0x00 */
    alarm.levelOneCellVoltageTooHigh = bitRead(payload[0], 0);
    alarm.levelTwoCellVoltageTooHigh = bitRead(payload[0], 1);
    alarm.levelOneCellVoltageTooLow  = bitRead(payload[0], 2);
    alarm.levelTwoCellVoltageTooLow  = bitRead(payload[0], 3);
    alarm.levelOnePackVoltageTooHigh = bitRead(payload[0], 4);
    alarm.levelTwoPackVoltageTooHigh = bitRead(payload[0], 5);
    alarm.levelOnePackVoltageTooLow  = bitRead(payload[0], 6);
    alarm.levelTwoPackVoltageTooLow  = bitRead(payload[0], 7);

    // /* 0x01 */
    alarm.levelOneChargeTempTooHigh    = bitRead(payload[1], 0);
    alarm.levelTwoChargeTempTooHigh    = bitRead(payload[1], 1);
    alarm.levelOneChargeTempTooLow     = bitRead(payload[1], 2);
    alarm.levelTwoChargeTempTooLow     = bitRead(payload[1], 3);
    alarm.levelOneDischargeTempTooHigh = bitRead(payload[1], 4);
    alarm.levelTwoDischargeTempTooHigh = bitRead(payload[1], 5);
    alarm.levelOneDischargeTempTooLow  = bitRead(payload[1], 6);
    alarm.levelTwoDischargeTempTooLow  = bitRead(payload[1], 7);

    // /* 0x02 */
    alarm.levelOneChargeCurrentTooHigh    = bitRead(payload[2], 0);
    alarm.levelTwoChargeCurrentTooHigh    = bitRead(payload[2], 1);
    alarm.levelOneDischargeCurrentTooHigh = bitRead(payload[2], 2);
    alarm.levelTwoDischargeCurrentTooHigh = bitRead(payload[2], 3);
    alarm.levelOneStateOfChargeTooHigh    = bitRead(payload[2], 4);
    alarm.levelTwoStateOfChargeTooHigh    = bitRead(payload[2], 5);
    alarm.levelOneStateOfChargeTooLow     = bitRead(payload[2], 6);
    alarm.levelTwoStateOfChargeTooLow     = bitRead(payload[2], 7);

    // /* 0x03 */
    alarm.levelOneCellVoltageDifferenceTooHigh = bitRead(payload[3], 0);
    alarm.levelTwoCellVoltageDifferenceTooHigh = bitRead(payload[3], 1);
    alarm.levelOneTempSensorDifferenceTooHigh  = bitRead(payload[3], 2);
    alarm.levelTwoTempSensorDifferenceTooHigh  = bitRead(payload[3], 3);

    // /* 0x04 */
    alarm.chargeFETTemperatureTooHigh            = bitRead(payload[4], 0);
    alarm.dischargeFETTemperatureTooHigh         = bitRead(payload[4], 1);
    alarm.failureOfChargeFETTemperatureSensor    = bitRead(payload[4], 2);
    alarm.failureOfDischargeFETTemperatureSensor = bitRead(payload[4], 3);
    alarm.failureOfChargeFETAdhesion             = bitRead(payload[4], 4);
    alarm.failureOfDischargeFETAdhesion          = bitRead(payload[4], 5);
    alarm.failureOfChargeFETTBreaker             = bitRead(payload[4], 6);
    alarm.failureOfDischargeFETBreaker           = bitRead(payload[4], 7);

    // /* 0x05 */
    alarm.failureOfAFEAcquisitionModule        = bitRead(payload[5], 0);
    alarm.failureOfVoltageSensorModule         = bitRead(payload[5], 1);
    alarm.failureOfTemperatureSensorModule     = bitRead(payload[5], 2);
    alarm.failureOfEEPROMStorageModule         = bitRead(payload[5], 3);
    alarm.failureOfRealtimeClockModule         = bitRead(payload[5], 4);
    alarm.failureOfPrechargeModule             = bitRead(payload[5], 5);
    alarm.failureOfVehicleCommunicationModule  = bitRead(payload[5], 6);
    alarm.failureOfIntranetCommunicationModule = bitRead(payload[5], 7);

    // /* 0x06 */
    alarm.failureOfCurrentSensorModule     = bitRead(payload[6], 0);
    alarm.failureOfMainVoltageSensorModule = bitRead(payload[6], 1);
    alarm.failureOfShortCircuitProtection  = bitRead(payload[6], 2);
    alarm.failureOfLowVoltageNoCharging    = bitRead(payload[6], 3);
   
    return ret;
}

void Daly100Bms::print_cell_voltages() 
{
    ROVER_LOGLN("");
    ROVER_LOGLN("--- DALY FRAME CMD=0x95 ---");
    ROVER_LOGLN("===============================================");
    ROVER_LOGLN("--- Cell Voltages ---");

    for (int i = 0; i < expectedCellCount; ++i) {
        const uint16_t mv = static_cast<uint16_t>(get.cellVmV[i]);
        const float v = mv / 1000.0f;
        ROVER_LOGF("Cell %02d: %u mV (%.3f V)\n", i + 1, mv, v);
    }
    
    ROVER_LOGLN("--- End Cell Voltages ---");
    ROVER_LOGLN("===============================================");
}

void Daly100Bms::create_request(uint8_t cmd, uint8_t *request, const uint8_t payload8[8]) 
{
    if (cmd == 0x95) {
        reset_cell_assembly();
        frame_base_detected_ = false;
    }

    uint8_t pkt[13];
    pkt[0] = 0xA5;
    pkt[1] = 0x40;
    pkt[2] = cmd;
    pkt[3] = 0x08;
    
    for (int i = 0; i < 8; ++i) {
        pkt[4 + i] = payload8 ? payload8[i] : 0x00;
    }
    
    uint8_t sum = 0;
    
    for (int i = 0; i < 12; ++i) {
        sum += pkt[i];
    }
    
    pkt[12] = sum;

    memcpy(request, pkt, sizeof(pkt));
}

void Daly100Bms::decode_response(const uint8_t *frame)
{
    if (frame[0] != 0xA5) {
        return;
    }
    
    if (!is_valid_frame(frame, kFrameSize)) {
        return;
    }

    ++decoded_frame_count_;

    uint8_t cmd  = frame[2];
    const uint8_t *payload = frame + 4; // payload[0..7]

    switch (cmd) {
        case VOUT_IOUT_SOC: {
            (void)get_pack_measurements(payload);
#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x90 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("Basic Status Info: Voltage, Current, SOC");
            ROVER_LOGLN("------------------------------------------------");
            ROVER_LOGF("Total Voltage: %.1f V\n", get.packVoltage);
            ROVER_LOGF("Current: %.1f A\n", get.packCurrent);
            ROVER_LOGF("SOC: %.1f %%\n", get.packSOC);
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case MIN_MAX_CELL_VOLTAGE: {
            (void)get_min_max_cell_voltage(payload);
#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x91 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("Min/Max Cell Voltage Info:");
            ROVER_LOGLN("------------------------------------------------");
            ROVER_LOGF("MaxCell %d: %.0f mV\n", get.maxCellVNum, get.maxCellmV);
            ROVER_LOGF("MinCell %d: %.0f mV\n", get.minCellVNum, get.minCellmV);
            ROVER_LOGF("Voltage Difference: %.0f mV\n", get.cellDiff);
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case MIN_MAX_TEMPERATURE: {
            (void)get_pack_temp(payload);
#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x92 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("Temperature Info:");
            ROVER_LOGLN("------------------------------------------------");
            ROVER_LOGF("MaxTemp: %.1f C \n", get.tempMax);
            ROVER_LOGF("MinTemp: %.1f C \n", get.tempMin);
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case DISCHARGE_CHARGE_MOS_STATUS: {
            (void)get_discharge_charge_mosfet_status(payload);
            
            const char* stateStr = "Unknown";
            if (get.chargeDischargeStatus == 0) {
                stateStr = "Stationary";
            } else if (get.chargeDischargeStatus == 1) {
                stateStr = "Charging";
            } else if (get.chargeDischargeStatus == 2) {
                stateStr = "Discharging";
            }
            
            const char* chargeMOSstr    = (get.chargeFetState == true)    ? "ON" : "OFF";
            const char* dischargeMOSstr = (get.disChargeFetState == true) ? "ON" : "OFF";

#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x93 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("BMS State Info:");
            ROVER_LOGLN("------------------------------------------------");
            ROVER_LOGF("State: %s\n", stateStr);
            ROVER_LOGF("Charge MOS: %s\n", chargeMOSstr);
            ROVER_LOGF("Discharge MOS: %s\n", dischargeMOSstr);
            ROVER_LOGF("Heart beat: %u\n", get.bmsHeartBeat);
            ROVER_LOGF("Remaining Capacity: %u mAh\n", get.resCapacitymAh);
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case STATUS_INFO: {

            (void)get_status_info(payload);

            uint8_t batteryStrings = payload[0];  // number of cells
            uint8_t tempCount      = payload[1];  // temp sensors
            uint8_t chargerStatus  = payload[2];  // 0=disconnected, 1=connected
            uint8_t loadStatus     = payload[3];  // 0=disconnected, 1=connected
            uint16_t cycles = (payload[5] << 8) | payload[6]; // bytes 5-6

#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x94 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("Battery Configuration Info:");
            ROVER_LOGLN("------------------------------------------------");
            ROVER_LOGF("Cells: %d cells\n", get.numberOfCells);
            ROVER_LOGF("Temperature Sensors: %d\n", get.numOfTempSensors);
            ROVER_LOGF("Charger Status: %s\n", get.chargeState ? "Connected" : "Disconnected");
            ROVER_LOGF("Load Status: %s\n", get.loadState ? "Connected" : "Disconnected");
            ROVER_LOGF("Charge Cycles: %d\n", get.bmsCycles);
            ROVER_LOGLN("===============================================");
#endif
            if (get.numberOfCells >= 1 && get.numberOfCells <= 48) {
                expectedCellCount = get.numberOfCells;
                recompute_expected_frames();
                reset_cell_assembly();
            }
            break;
        }
        case CELL_VOLTAGES: {
            (void)get_cell_voltages(payload);
            break;
        }
        case CELL_TEMPERATURE: {
            (void)get_cell_temperature(payload);
#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x96 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("--- Cell Temperatures ---");

            for (int i = 0; i < get.numOfTempSensors; ++i) {
                ROVER_LOGF("Cell %02d: %d C \n", i + 1, get.cellTemperature[i]);
            }
            
            ROVER_LOGLN("--- End Cell Temperatures ---");
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case CELL_BALANCE_STATE: {
            get_cell_balance_state(payload);
#if ROVER_DEBUG_BMS
            ROVER_LOGLN("");
            ROVER_LOGLN("--- DALY FRAME CMD=0x97 ---");
            ROVER_LOGLN("===============================================");
            ROVER_LOGLN("Cell Balance State (0=Closed, 1=Open):");
            ROVER_LOGLN("------------------------------------------------");
            for (int i = 0; i < expectedCellCount; ++i) {
                ROVER_LOGF("Cell %02d: %s\n", i + 1, get.cellBalanceState[i] ? "Open" : "Closed");
            }
            ROVER_LOGLN("===============================================");
#endif
            break;
        }
        case FAILURE_CODES: {
            get_failure_codes(payload);
            break;
        }
        default:
            break;
    }
}

} // daly100_bms