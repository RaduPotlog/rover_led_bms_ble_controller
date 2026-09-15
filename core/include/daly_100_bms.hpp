#ifndef DALY_100_BMS_HPP
#define DALY_100_BMS_HPP

#include <Arduino.h>
#include <stdint.h>
#include <cstring>

/// @brief Portable struct packing.
/// @note The wire layout of BmsData and Alarm is load-bearing -- the UDP telemetry
///       payload is these two structs' bytes, and a receiver off-board parses them
///       at fixed offsets. __attribute__((packed)) is a GCC/Clang spelling that MSVC
///       rejects outright, so the host test build needs an equivalent rather than a
///       silent fallback to default padding.
#if defined(_MSC_VER)
#  define ROVER_PACK_BEGIN __pragma(pack(push, 1))
#  define ROVER_PACK_END   __pragma(pack(pop))
#  define ROVER_PACKED
#else
#  define ROVER_PACK_BEGIN
#  define ROVER_PACK_END
#  define ROVER_PACKED     __attribute__((packed))
#endif

namespace daly100_bms
{

/// @brief 
class Daly100Bms
{

public:

    /// @brief Largest pack the decoder can represent, and the size of cellVmV
    ///        and cellBalanceState. 0x97 reports balance state as 6 bytes of
    ///        bit flags, which is exactly this many cells.
    static constexpr int kMaxCells = 48;

    /// @brief Size of cellTemperature; the largest sensor count 0x96 can fill.
    static constexpr int kMaxTempSensors = 16;

    /// @brief 
    enum ReturnStatus
    {
        E_NOK   = 0x00U,
        E_OK    = 0x01U,
        E_BUSY  = 0x03U,
    };

    /// @brief 
    enum Command : uint8_t
    {
        BMS_RESET                   = 0x00U,
        VOUT_IOUT_SOC               = 0x90U,
        MIN_MAX_CELL_VOLTAGE        = 0x91U,
        MIN_MAX_TEMPERATURE         = 0x92U,
        DISCHARGE_CHARGE_MOS_STATUS = 0x93U,
        STATUS_INFO                 = 0x94U,
        CELL_VOLTAGES               = 0x95U,
        CELL_TEMPERATURE            = 0x96U,
        CELL_BALANCE_STATE          = 0x97U,
        FAILURE_CODES               = 0x98U,
        DISCHRG_FET                 = 0xD9U,
        CHRG_FET                    = 0xDAU,
       
        END_OF_ENUM                 = 0xFFU,
    };

    /**
     * @brief get struct holds all the data collected from the BMS
     * @details Comments give the units after decoding: the raw Daly units (0.1 V,
     *          0.1 A with 30000 offset, 0.1 %, +40 °C offset) are converted in the
     *          get_*() parsers. rover_battery (ROS 2) mirrors this layout in
     *          rover_battery/domain/bms_frame.hpp -- change both together.
     */
    ROVER_PACK_BEGIN
    struct BmsData
    {
        // Pack the variables directly into matching memory slices
        struct {
            // data from 0x90
            float packVoltage; // Total pack voltage (V)
            float packCurrent; // Current in (+, charging) or out (-, discharging) of pack (A)
            float packSOC;     // State Of Charge (%, 0-100)

            // data from 0x91
            float maxCellmV; // Maximum cell voltage (mV)
            int maxCellVNum; // Number of cell with highest voltage
            float minCellmV; // Minimum cell voltage (mV)
            int minCellVNum; // Number of cell with lowest voltage
            float cellDiff;  // Difference between min and max cell voltages (mV)

            // data from 0x92
            float tempMax;      // Maximum temperature sensor reading (°C)
            float tempMin;      // Minimum temperature sensor reading (°C)
            float tempAverage;  // (tempMax + tempMin) / 2 (°C)

            // data from 0x93
            int chargeDischargeStatus;    // charge/discharge status (0 stationary, 1 charge, 2 discharge)
            bool chargeFetState;          // charging MOSFET status
            bool disChargeFetState;       // discharge MOSFET state
            int bmsHeartBeat;             // BMS life (0~255 cycles)?
            int resCapacitymAh;           // residual capacity mAH

            // data from 0x94
            int numberOfCells;    // Cell count
            int numOfTempSensors; // Temp sensor count
            bool chargeState;     // charger status 0 = disconnected 1 = connected
            bool loadState;       // Load Status 0=disconnected 1=connected
            bool dIO[8];          // No information about this
            int bmsCycles;        // charge / discharge cycles

            // data from 0x95
            float cellVmV[kMaxCells]; // Store Cell Voltages (mV)

            // data from 0x96
            int cellTemperature[kMaxTempSensors]; // array of cell Temperature sensors

            // data from 0x97
            bool cellBalanceState[kMaxCells]; // bool array of cell balance states
            bool cellBalanceActive;    // bool is cell balance active
        } ROVER_PACKED; // Forces the compiler to omit padding gaps
    } get{};
    ROVER_PACK_END

    /**
     * @brief alarm struct holds booleans corresponding to all the possible alarms 
     *        (aka errors/warnings) the BMS can report
     */
    ROVER_PACK_BEGIN
    struct Alarm {
        // Pack the variables directly into matching memory slices
        struct {
            /* 0x00 */
            uint8_t levelOneCellVoltageTooHigh   : 1;
            uint8_t levelTwoCellVoltageTooHigh   : 1;
            uint8_t levelOneCellVoltageTooLow    : 1;
            uint8_t levelTwoCellVoltageTooLow    : 1;
            uint8_t levelOnePackVoltageTooHigh   : 1;
            uint8_t levelTwoPackVoltageTooHigh   : 1;
            uint8_t levelOnePackVoltageTooLow    : 1;
            uint8_t levelTwoPackVoltageTooLow    : 1;

            /* 0x01 */
            uint8_t levelOneChargeTempTooHigh    : 1;
            uint8_t levelTwoChargeTempTooHigh    : 1;
            uint8_t levelOneChargeTempTooLow     : 1;
            uint8_t levelTwoChargeTempTooLow     : 1;
            uint8_t levelOneDischargeTempTooHigh : 1;
            uint8_t levelTwoDischargeTempTooHigh : 1;
            uint8_t levelOneDischargeTempTooLow  : 1;
            uint8_t levelTwoDischargeTempTooLow  : 1;

            /* 0x02 */
            uint8_t levelOneChargeCurrentTooHigh    : 1;
            uint8_t levelTwoChargeCurrentTooHigh    : 1;
            uint8_t levelOneDischargeCurrentTooHigh : 1;
            uint8_t levelTwoDischargeCurrentTooHigh : 1;
            uint8_t levelOneStateOfChargeTooHigh    : 1;
            uint8_t levelTwoStateOfChargeTooHigh    : 1;
            uint8_t levelOneStateOfChargeTooLow     : 1;
            uint8_t levelTwoStateOfChargeTooLow     : 1;

            /* 0x03 */
            uint8_t levelOneCellVoltageDifferenceTooHigh : 1;
            uint8_t levelTwoCellVoltageDifferenceTooHigh : 1;
            uint8_t levelOneTempSensorDifferenceTooHigh  : 1;
            uint8_t levelTwoTempSensorDifferenceTooHigh  : 1;
            uint8_t : 4; // Padding bits to finish the 0x03 byte boundary

            /* 0x04 */
            uint8_t chargeFETTemperatureTooHigh            : 1;
            uint8_t dischargeFETTemperatureTooHigh         : 1;
            uint8_t failureOfChargeFETTemperatureSensor    : 1;
            uint8_t failureOfDischargeFETTemperatureSensor : 1;
            uint8_t failureOfChargeFETAdhesion             : 1;
            uint8_t failureOfDischargeFETAdhesion          : 1;
            uint8_t failureOfChargeFETTBreaker             : 1;
            uint8_t failureOfDischargeFETBreaker           : 1;

            /* 0x05 */
            uint8_t failureOfAFEAcquisitionModule        : 1;
            uint8_t failureOfVoltageSensorModule         : 1;
            uint8_t failureOfTemperatureSensorModule     : 1;
            uint8_t failureOfEEPROMStorageModule         : 1;
            uint8_t failureOfRealtimeClockModule         : 1;
            uint8_t failureOfPrechargeModule             : 1;
            uint8_t failureOfVehicleCommunicationModule   : 1;
            uint8_t failureOfIntranetCommunicationModule  : 1;

            /* 0x06 */
            uint8_t failureOfCurrentSensorModule     : 1;
            uint8_t failureOfMainVoltageSensorModule : 1;
            uint8_t failureOfShortCircuitProtection  : 1;
            uint8_t failureOfLowVoltageNoCharging    : 1;
            uint8_t : 4; // Padding bits to finish the 0x06 byte boundary
        } ROVER_PACKED; // Forces the compiler to omit padding gaps
    } alarm{};
    ROVER_PACK_END

    /// @brief Construct a decoder with all decoded state zeroed.
    /// @note get and alarm carry brace initialisers because nothing else zeroes
    ///       them. On the target this class is instantiated as a file-scope static
    ///       and so was zero-initialised by storage duration, which masked the gap;
    ///       any other storage class read uninitialised values until a frame for
    ///       that field arrived.
    explicit Daly100Bms() 
    {

    }

    /// @brief 
    virtual ~Daly100Bms()
    {

    }

    /// @brief 
    void print_cell_voltages();

    /// @brief 
    /// @param cmd 
    /// @param request 
    /// @param payload8 
    void create_request(uint8_t cmd, uint8_t *request, const uint8_t payload8[8] = nullptr);

    /// @brief 
    /// @param frame 
    void decode_response(const uint8_t *frame);

    /// @brief Push received bytes into the reassembly buffer and decode any whole
    ///        frames they complete.
    /// @param data Bytes as they arrived; need not align to frame boundaries.
    /// @param len Number of bytes.
    /// @note BLE notifications carry an arbitrary slice of the 13-byte frame
    ///       stream, so resynchronisation lives here rather than at the call site.
    ///       A candidate sync byte is accepted only if the 13 bytes starting there
    ///       also checksum; otherwise the scan advances by one byte. Advancing by a
    ///       whole frame instead would swallow the next real frame whenever 0xA5
    ///       appeared inside a payload.
    void feed(const uint8_t *data, size_t len);

    /// @brief Test whether a buffer holds a well-formed frame.
    /// @param pkt Candidate frame.
    /// @param len Length of pkt.
    /// @return true if the trailing byte matches the sum of those before it.
    static bool is_valid_frame(const uint8_t *pkt, size_t len);

    
    float get_pack_voltage() 
    {
        return get.packVoltage;
    }

    float get_pack_current() 
    {
        return get.packCurrent;
    }

    float get_pack_soc() 
    {
        return get.packSOC;
    }

    float get_pack_temp_max() 
    {
        return get.tempMax;
    }

    int get_charge_discharge_status() 
    {
        return get.chargeDischargeStatus;
    }

    BmsData *get_data() 
    {
        return &get;
    }
    
    Alarm *get_alarm() 
    {
        return &alarm; 
    }

    bool get_charge_fet_state() 
    {
        return get.chargeFetState;     
    }

    bool get_discharge_fet_state()
    {
        return get.disChargeFetState;     
    }

    /// @brief Number of valid frames decoded since construction.
    /// @note Lets the caller tell fresh data from a BMS that stopped answering while
    ///       the BLE link stays up: if the count did not move over a whole poll
    ///       cycle, BmsData only holds old values. Wraps harmlessly; compare for
    ///       inequality, not order.
    uint32_t decoded_frame_count() const
    {
        return decoded_frame_count_;
    }

    /// @brief Number of 0x95 frames needed to assemble a full set of cell voltages.
    /// @note Derived from the cell count reported by 0x94. Exposed for tests.
    int get_expected_frame_count() const
    {
        return expectedFramesNeeded;
    }

private:

    /// @brief 
    int expectedCellCount = 12;
    
    /// @brief 
    int expectedFramesNeeded = (expectedCellCount + 2) / 3;
    
    /// @brief 
    void reset_cell_assembly();
    
    
    /// @brief 
    void recompute_expected_frames();

    /// @brief 0x90
    /// @return 
    ReturnStatus get_pack_measurements(const uint8_t *payload);

    /// @brief 0x91
    /// @param payload 
    /// @return 
    ReturnStatus get_min_max_cell_voltage(const uint8_t *payload);

    /// @brief 0x92
    /// @param payload 
    /// @return 
    ReturnStatus get_pack_temp(const uint8_t *payload);

    /// @brief 0x93
    /// @param payload 
    /// @return 
    ReturnStatus get_discharge_charge_mosfet_status(const uint8_t *payload);

    /// @brief 0x94
    /// @param payload 
    /// @return 
    ReturnStatus get_status_info(const uint8_t *payload);

    /// @brief 0x95
    /// @param payload 
    /// @return 
    ReturnStatus get_cell_voltages(const uint8_t *payload);

    /// @brief 0x96
    /// @param payload 
    /// @return 
    ReturnStatus get_cell_temperature(const uint8_t *payload);

    /// @brief 0x97
    /// @param payload 
    /// @return 
    ReturnStatus get_cell_balance_state(const uint8_t *payload);
    
    /// @brief 0x98
    /// @return 
    ReturnStatus get_failure_codes(const uint8_t *payload);
    
    /// @brief Wire size of every Daly request and response frame.
    static constexpr size_t kFrameSize = 13;

    /// @brief Reassembly buffer for the BLE notification byte stream.
    /// @note Sized for a healthy backlog of frames rather than a single one; the
    ///       longest burst is the four-frame 0x95 cell voltage set at 52 bytes.
    static constexpr size_t kRxBufferSize = 256;

    /// @brief Bytes received but not yet consumed as whole frames.
    uint8_t rx_buf_[kRxBufferSize];

    /// @brief Number of valid bytes in rx_buf_.
    size_t rx_len_{0};

    /// @brief Which 0x95 frames of the current cell voltage set have arrived.
    /// @note Initialised here as well as by reset_cell_assembly(), because a 0x95
    ///       response that arrives before any 0x94 or 0x95 request would otherwise
    ///       be indexed against uninitialised flags.
    bool frame_received_[16]{};

    int frames_received_count_{0};

    /// @brief See decoded_frame_count().
    uint32_t decoded_frame_count_{0};

    bool assembled_{false};

    bool frame_base_detected_{false};

    int frame_base_{0};
};

} // daly100_bms

#endif // DALY_100_BMS_HPP