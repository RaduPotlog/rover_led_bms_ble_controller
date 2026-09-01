#include <string.h>

#include "bms_telemetry_codec.hpp"

namespace telemetry
{

namespace {

/// @brief Append one value to buffer and return the new offset.
template<typename T>
size_t pack(uint8_t *buf, size_t offset, const T &value)
{
    memcpy(buf + offset, &value, sizeof(T));
    return offset + sizeof(T);
}

} // namespace

size_t BmsTelemetryCodec::serialize_data(
    const daly100_bms::Daly100Bms::BmsData *data,
    uint8_t *buffer)
{
    size_t offset = 0;

    offset = pack(buffer, offset, data->packVoltage);
    offset = pack(buffer, offset, data->packCurrent);
    offset = pack(buffer, offset, data->packSOC);
    offset = pack(buffer, offset, data->maxCellmV);
    offset = pack(buffer, offset, data->maxCellVNum);
    offset = pack(buffer, offset, data->minCellmV);
    offset = pack(buffer, offset, data->minCellVNum);
    offset = pack(buffer, offset, data->cellDiff);
    offset = pack(buffer, offset, data->tempMax);
    offset = pack(buffer, offset, data->tempMin);
    offset = pack(buffer, offset, data->tempAverage);
    offset = pack(buffer, offset, data->chargeDischargeStatus);
    offset = pack(buffer, offset, data->chargeFetState);
    offset = pack(buffer, offset, data->disChargeFetState);
    offset = pack(buffer, offset, data->bmsHeartBeat);
    offset = pack(buffer, offset, data->resCapacitymAh);
    offset = pack(buffer, offset, data->numberOfCells);
    offset = pack(buffer, offset, data->numOfTempSensors);
    offset = pack(buffer, offset, data->chargeState);
    offset = pack(buffer, offset, data->loadState);
    memcpy(buffer + offset, data->dIO, sizeof(data->dIO)); offset += sizeof(data->dIO);
    offset = pack(buffer, offset, data->bmsCycles);
    memcpy(buffer + offset, data->cellVmV, sizeof(data->cellVmV)); offset += sizeof(data->cellVmV);
    memcpy(buffer + offset, data->cellTemperature, sizeof(data->cellTemperature)); offset += sizeof(data->cellTemperature);
    memcpy(buffer + offset, data->cellBalanceState, sizeof(data->cellBalanceState)); offset += sizeof(data->cellBalanceState);
    offset = pack(buffer, offset, data->cellBalanceActive);

    return offset;
}

size_t BmsTelemetryCodec::serialize_alarm(
    const daly100_bms::Daly100Bms::Alarm *alarm,
    uint8_t *buffer)
{
    memset(buffer, 0, kAlarmSize);

    /* Byte 0x00 */
    if (alarm->levelOneCellVoltageTooHigh)  buffer[0] |= (1 << 0);
    if (alarm->levelTwoCellVoltageTooHigh)  buffer[0] |= (1 << 1);
    if (alarm->levelOneCellVoltageTooLow)   buffer[0] |= (1 << 2);
    if (alarm->levelTwoCellVoltageTooLow)   buffer[0] |= (1 << 3);
    if (alarm->levelOnePackVoltageTooHigh)  buffer[0] |= (1 << 4);
    if (alarm->levelTwoPackVoltageTooHigh)  buffer[0] |= (1 << 5);
    if (alarm->levelOnePackVoltageTooLow)   buffer[0] |= (1 << 6);
    if (alarm->levelTwoPackVoltageTooLow)   buffer[0] |= (1 << 7);

    /* Byte 0x01 */
    if (alarm->levelOneChargeTempTooHigh)     buffer[1] |= (1 << 0);
    if (alarm->levelTwoChargeTempTooHigh)     buffer[1] |= (1 << 1);
    if (alarm->levelOneChargeTempTooLow)      buffer[1] |= (1 << 2);
    if (alarm->levelTwoChargeTempTooLow)      buffer[1] |= (1 << 3);
    if (alarm->levelOneDischargeTempTooHigh)  buffer[1] |= (1 << 4);
    if (alarm->levelTwoDischargeTempTooHigh)  buffer[1] |= (1 << 5);
    if (alarm->levelOneDischargeTempTooLow)   buffer[1] |= (1 << 6);
    if (alarm->levelTwoDischargeTempTooLow)   buffer[1] |= (1 << 7);

    /* Byte 0x02 */
    if (alarm->levelOneChargeCurrentTooHigh)     buffer[2] |= (1 << 0);
    if (alarm->levelTwoChargeCurrentTooHigh)     buffer[2] |= (1 << 1);
    if (alarm->levelOneDischargeCurrentTooHigh)  buffer[2] |= (1 << 2);
    if (alarm->levelTwoDischargeCurrentTooHigh)  buffer[2] |= (1 << 3);
    if (alarm->levelOneStateOfChargeTooHigh)     buffer[2] |= (1 << 4);
    if (alarm->levelTwoStateOfChargeTooHigh)     buffer[2] |= (1 << 5);
    if (alarm->levelOneStateOfChargeTooLow)      buffer[2] |= (1 << 6);
    if (alarm->levelTwoStateOfChargeTooLow)      buffer[2] |= (1 << 7);

    /* Byte 0x03 */
    if (alarm->levelOneCellVoltageDifferenceTooHigh) buffer[3] |= (1 << 0);
    if (alarm->levelTwoCellVoltageDifferenceTooHigh) buffer[3] |= (1 << 1);
    if (alarm->levelOneTempSensorDifferenceTooHigh)  buffer[3] |= (1 << 2);
    if (alarm->levelTwoTempSensorDifferenceTooHigh)  buffer[3] |= (1 << 3);

    /* Byte 0x04 */
    if (alarm->chargeFETTemperatureTooHigh)            buffer[4] |= (1 << 0);
    if (alarm->dischargeFETTemperatureTooHigh)         buffer[4] |= (1 << 1);
    if (alarm->failureOfChargeFETTemperatureSensor)    buffer[4] |= (1 << 2);
    if (alarm->failureOfDischargeFETTemperatureSensor) buffer[4] |= (1 << 3);
    if (alarm->failureOfChargeFETAdhesion)             buffer[4] |= (1 << 4);
    if (alarm->failureOfDischargeFETAdhesion)          buffer[4] |= (1 << 5);
    if (alarm->failureOfChargeFETTBreaker)             buffer[4] |= (1 << 6);
    if (alarm->failureOfDischargeFETBreaker)           buffer[4] |= (1 << 7);

    /* Byte 0x05 */
    if (alarm->failureOfAFEAcquisitionModule)        buffer[5] |= (1 << 0);
    if (alarm->failureOfVoltageSensorModule)         buffer[5] |= (1 << 1);
    if (alarm->failureOfTemperatureSensorModule)     buffer[5] |= (1 << 2);
    if (alarm->failureOfEEPROMStorageModule)         buffer[5] |= (1 << 3);
    if (alarm->failureOfRealtimeClockModule)         buffer[5] |= (1 << 4);
    if (alarm->failureOfPrechargeModule)             buffer[5] |= (1 << 5);
    if (alarm->failureOfVehicleCommunicationModule)  buffer[5] |= (1 << 6);
    if (alarm->failureOfIntranetCommunicationModule) buffer[5] |= (1 << 7);

    /* Byte 0x06 */
    if (alarm->failureOfCurrentSensorModule)         buffer[6] |= (1 << 0);
    if (alarm->failureOfMainVoltageSensorModule)     buffer[6] |= (1 << 1);
    if (alarm->failureOfShortCircuitProtection)      buffer[6] |= (1 << 2);
    if (alarm->failureOfLowVoltageNoCharging)        buffer[6] |= (1 << 3);

    return kAlarmSize;
}

size_t BmsTelemetryCodec::serialize(
    const daly100_bms::Daly100Bms::BmsData *data,
    const daly100_bms::Daly100Bms::Alarm *alarm,
    uint8_t *buffer,
    size_t buffer_len)
{
    if (!data || !alarm || !buffer || buffer_len < kPacketSize) {
        return 0;
    }

    size_t offset = serialize_data(data, buffer);

    offset += serialize_alarm(alarm, buffer + offset);

    return offset;
}

} // namespace telemetry
