#include <Arduino.h>
#include <WiFi.h>

#include "config.hpp"
#include "logging.hpp"
#include "connector_interface.hpp"
#include "wifi_connector.hpp"
#include "udp_connection.hpp"
#include "ble_connection.hpp"
#include "daly_100_bms.hpp"
#include "led_controller.hpp"

#include "EasyNextionLibrary.h"

/* ==========================================================================
 * Globals
 * ==========================================================================
 * One WiFi link and one BLE link, shared by two independent UDP sockets:
 * gBmsUdpConnection pushes serialized BMS telemetry out, gLedUdpConnection
 * receives colour frames for the strip. UdpConnection holds a per-instance
 * AsyncUDP, so the two ports coexist without further plumbing.
 */

static connector::ConnectorInterface *gWifiConnectionInterface = nullptr;
static connector::BleConnection *gBleConnectionInterface = nullptr;
static connector::UdpConnection gBmsUdpConnection;
static connector::UdpConnection gLedUdpConnection;
static connector::BleClient gBleClient;
static daly100_bms::Daly100Bms gDaly100Bms;
static led_controller::LedController gLedController;
static EasyNex gNexDisplay(Serial);

connector::BleClient::rx_callback connector::BleClient::rx_callback_ = nullptr;

/* --- BLE receive stream ------------------------------------------------- */

static uint8_t rxStreamBuf[1024];
static size_t rxStreamLen = 0;

uint8_t cmd_buff[] = { 0x94, 0x90, 0x91, 0x92, 0x93, 0x95, 0x96, 0x97, 0x98 };

/* --- BMS telemetry payload ---------------------------------------------- */

struct UdpData {
    daly100_bms::Daly100Bms::BmsData *data;
    daly100_bms::Daly100Bms::Alarm *alarm;
};

static UdpData udp_data;
static uint8_t udp_buff[sizeof(daly100_bms::Daly100Bms::BmsData) + sizeof(daly100_bms::Daly100Bms::Alarm)];

/* --- LED strip -----------------------------------------------------------
 * Ownership rule, carried over from the standalone LED firmware: while the link
 * is down, loop() drives the strip with the link-status animation. On the first
 * successful connect it hands ownership to the UDP receive callback and stops
 * writing. gIsLedUdpOwner gates the callback so it cannot paint over the
 * animation before the link is up.
 */

static uint32_t red_color_led_buff[config::kNumLeds];
static uint32_t blue_color_led_buff[config::kNumLeds];
static uint32_t cleared_color_led_buff[config::kNumLeds];

// Written from loop(), read from the AsyncUDP task -- volatile so the compiler
// cannot cache it across the two contexts.
static volatile bool gIsLedUdpOwner = false;

/* ==========================================================================
 * BLE frame assembly
 * ========================================================================== */

void processRxStream()
{
    while (rxStreamLen >= 13) {

        if (rxStreamBuf[0] != 0xA5) {
            size_t pos = 0;

            while (pos < rxStreamLen && rxStreamBuf[pos] != 0xA5) ++pos;

            if (pos == rxStreamLen) {
                rxStreamLen = 0;
                return;
            }

            if (pos > 0) {
                memmove(rxStreamBuf, rxStreamBuf + pos, rxStreamLen - pos);
                rxStreamLen -= pos;
            }

            if (rxStreamLen < 13) {
                return;
            }
        }

        uint8_t frame[13];
        memcpy(frame, rxStreamBuf, 13);
        memmove(rxStreamBuf, rxStreamBuf + 13, rxStreamLen - 13);
        rxStreamLen -= 13;
        gDaly100Bms.decode_response(frame);
    }
}

void ble_rx_callback(uint8_t *data, size_t length)
{
    if (rxStreamLen + length > sizeof(rxStreamBuf)) {
        rxStreamLen = 0;
    }

    memcpy(rxStreamBuf + rxStreamLen, data, length);
    rxStreamLen += length;
    processRxStream();
}

/* ==========================================================================
 * BMS telemetry serialization
 * ========================================================================== */

template<typename T>
size_t pack(uint8_t* buf, size_t offset, const T& value) {
    memcpy(buf + offset, &value, sizeof(T));
    return offset + sizeof(T);
}

size_t serializeData(const daly100_bms::Daly100Bms::BmsData* data, uint8_t* buffer)
{
    if (!data) {
        return 0;
    }

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

size_t serializeAlarm(const daly100_bms::Daly100Bms::Alarm* alarm, uint8_t* buffer)
{
    if (!alarm) {
        return 0;
    }

    // Clear buffer first
    memset(buffer, 0, 7);

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

    return 7;
}

/* ==========================================================================
 * LED strip
 * ========================================================================== */

/// @brief Drive the link-status animation on the strip.
/// @param is_link_up Current WiFi link state.
/// @note Runs only while the UDP receiver does not own the strip: a red blink on
///       a fixed wall-clock period while the link is down, and a single blue
///       flash on the down-to-up edge. The blink is millis()-based because this
///       loop() iterates roughly once per second (nine BLE writes at 100 ms
///       each), not at the 100 ms cadence the standalone LED firmware assumed.
static void update_led_link_status(const bool is_link_up)
{
    static unsigned long last_toggle_ms = 0;
    static bool is_red_light = true;

    if (is_link_up) {
        if (!gIsLedUdpOwner) {
            gLedController.update(blue_color_led_buff);
            gIsLedUdpOwner = true;
        }

        last_toggle_ms = millis();
        is_red_light = true;

        return;
    }

    // Link is down: take the strip back from the UDP receiver and blink.
    gIsLedUdpOwner = false;

    const unsigned long now = millis();

    if (now - last_toggle_ms >= config::kLedBlinkIntervalMs) {
        is_red_light = !is_red_light;
        last_toggle_ms = now;
    }

    gLedController.update(is_red_light ? red_color_led_buff : cleared_color_led_buff);
}

/* ==========================================================================
 * Arduino entry points
 * ========================================================================== */

void setup()
{
    Serial.begin(config::kSerialBaud);

    while (!Serial) { ; }

    ROVER_LOG_BEGIN();
    ROVER_LOGLN("Rover controller starting (BMS BLE + Nextion + UDP + LED)...");

    for (int i = 0; i < config::kNumLeds; i++) {
        red_color_led_buff[i] = 0x00FF0000UL;
        cleared_color_led_buff[i] = 0x00000000UL;
        blue_color_led_buff[i] = 0x000000FFUL;
    }

    connector::WifiConnector &wifi = connector::WifiConnector::getInstance();

#if ROVER_WIFI_USE_STATIC_IP
    wifi.set_static_ip(config::kStaticIp, config::kGateway, config::kSubnet, config::kDns1, config::kDns2);
#endif

    gWifiConnectionInterface = &wifi;
    gWifiConnectionInterface->connect(config::kWifiSsid, config::kWifiPass);

    gBmsUdpConnection.init(gWifiConnectionInterface, config::kBmsUdpPort, [](AsyncUDPPacket packet) {
        ROVER_LOGLN("Nothing to do with the received packet for now...");
    });

    gLedUdpConnection.init(gWifiConnectionInterface, config::kLedUdpPort, [](AsyncUDPPacket packet) {

        if (gIsLedUdpOwner == false) {
            return;
        }

        if (packet.length() < (config::kNumLeds * 2 * sizeof(uint16_t) + 8)) {
            ROVER_LOGLN("Received packet with invalid length");
            return;
        }

        gLedController.update(reinterpret_cast<const uint32_t *>(packet.data() + sizeof(uint32_t)));
    });

    gBleConnectionInterface = &connector::BleConnection::getInstance();
    gBleConnectionInterface->connect(config::kBleDeviceName, "");

    if (!gBleClient.init(gBleConnectionInterface, ble_rx_callback)) {
        ROVER_LOGLN("Client intialization failed");
    }

    gNexDisplay.writeStr("page main");
    delay(50);

    udp_data.data = gDaly100Bms.get_data();
    udp_data.alarm = gDaly100Bms.get_alarm();

    memset(udp_buff, 0, sizeof(udp_buff));
}

void loop()
{
    bool is_wifi_up = false;

    if (gWifiConnectionInterface != nullptr) {
        (void)gBmsUdpConnection.pool();
        (void)gLedUdpConnection.pool();

        is_wifi_up = gWifiConnectionInterface->is_connected();

        if (is_wifi_up) {
            gNexDisplay.writeNum("wifi_icon.pic", 3);
        } else {
            gNexDisplay.writeNum("wifi_icon.pic", 2);
        }
    }

    update_led_link_status(is_wifi_up);
    (void)gLedController.pool();

    if (gBleConnectionInterface != nullptr) {
        gBleClient.pool();

        uint8_t request[13];

        for (int i = 0; i < sizeof(cmd_buff) / sizeof(cmd_buff[0]); i++) {
            gDaly100Bms.create_request(cmd_buff[i], request);
            gBleClient.write(request, sizeof(request));
            delay(100);
        }

        if (gBleClient.is_connected()) {
            gNexDisplay.writeNum("ble_icon.pic", 1);
        } else {
            gNexDisplay.writeNum("ble_icon.pic", 2);
        }

        if (gWifiConnectionInterface) {

            if (gBleClient.is_connected()) {
                size_t offset = serializeData(udp_data.data, udp_buff);

                if (offset != 0) {
                    offset = serializeAlarm(udp_data.alarm, &udp_buff[offset]);
                    if (offset == 0) {
                        memset(udp_buff, 0, sizeof(udp_buff));
                    }
                } else {
                    memset(udp_buff, 0, sizeof(udp_buff));
                }
            } else {
                memset(udp_buff, 0, sizeof(udp_buff));
            }

            gBmsUdpConnection.send(udp_buff, sizeof(udp_buff));
        }
    }

    int state = gDaly100Bms.get_charge_discharge_status();

    if (state == 0) {
        gNexDisplay.writeStr("state.txt", "Stationary");
    } else if (state == 1) {
        gNexDisplay.writeStr("state.txt", "Charging");
    } else if (state == 2) {
        gNexDisplay.writeStr("state.txt", "Discharging");
    } else {
        gNexDisplay.writeStr("state.txt", "Unknown");
    }

    delay(10);
    gNexDisplay.writeStr("voltage_val.txt", String(gDaly100Bms.get_pack_voltage(), 2) + String(" V"));
    delay(10);
    gNexDisplay.writeStr("current_val.txt", String(gDaly100Bms.get_pack_current(), 2) + String(" A"));
    delay(10);
    gNexDisplay.writeStr("temp_val.txt", String(gDaly100Bms.get_pack_temp_max(), 2));
    delay(10);
    gNexDisplay.writeStr("soc_val.txt", String(gDaly100Bms.get_pack_soc(), 2));
    delay(10);

    int charge_mosfet_status_icon_idx = 0;
    int discharge_mosfet_status_icon_idx = 0;

    if (gDaly100Bms.get_charge_fet_state()) {
        charge_mosfet_status_icon_idx = 5;
    } else {
        charge_mosfet_status_icon_idx = 4;
    }

    if (gDaly100Bms.get_discharge_fet_state()) {
        discharge_mosfet_status_icon_idx = 5;
    } else {
        discharge_mosfet_status_icon_idx = 4;
    }

    gNexDisplay.writeNum("chg_mos_state.pic", charge_mosfet_status_icon_idx);
    delay(10);
    gNexDisplay.writeNum("dchg_mos_state.pic", discharge_mosfet_status_icon_idx);
    delay(10);
}
