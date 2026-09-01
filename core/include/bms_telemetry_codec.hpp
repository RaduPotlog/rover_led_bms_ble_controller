#ifndef BMS_TELEMETRY_CODEC_HPP
#define BMS_TELEMETRY_CODEC_HPP

#include <stdint.h>
#include <stddef.h>

#include "daly_100_bms.hpp"

namespace telemetry
{

/// @brief Serialises decoded BMS state into the outbound UDP payload.
///
/// @note The byte layout is frozen. A receiver off this board parses the payload
///       at fixed offsets and is not in this repository, so this is a lift of the
///       previous serializeData()/serializeAlarm() from main.cpp with the bytes
///       unchanged -- no version byte, no length prefix, no checksum. Adding any
///       of those is a coordinated change with that receiver.
///
///       What is new is the compile-time guard. The payload is the two protocol
///       structs' fields written out by hand, in order; adding a field to BmsData
///       without extending serialize() previously produced a silently truncated
///       packet and no diagnostic. The static assertions below turn that into a
///       build failure.
class BmsTelemetryCodec
{

public:

    /// @brief Bytes contributed by BmsData.
    static constexpr size_t kDataSize = sizeof(daly100_bms::Daly100Bms::BmsData);

    /// @brief Bytes contributed by Alarm: 52 flags packed into 7 bytes.
    static constexpr size_t kAlarmSize = 7;

    /// @brief Total payload size.
    static constexpr size_t kPacketSize = kDataSize + kAlarmSize;

    /// @brief Serialise data and alarm into buffer.
    /// @param data Decoded BMS state.
    /// @param alarm Decoded alarm flags.
    /// @param buffer Destination, at least kPacketSize bytes.
    /// @param buffer_len Size of buffer.
    /// @return Bytes written, or 0 if any argument was unusable.
    static size_t serialize(
        const daly100_bms::Daly100Bms::BmsData *data,
        const daly100_bms::Daly100Bms::Alarm *alarm,
        uint8_t *buffer,
        size_t buffer_len);

private:

    /// @brief Serialise just the BmsData fields.
    /// @return Bytes written.
    static size_t serialize_data(const daly100_bms::Daly100Bms::BmsData *data, uint8_t *buffer);

    /// @brief Serialise just the alarm bitfield.
    /// @return Bytes written.
    static size_t serialize_alarm(const daly100_bms::Daly100Bms::Alarm *alarm, uint8_t *buffer);
};

static_assert(
    sizeof(daly100_bms::Daly100Bms::Alarm) == BmsTelemetryCodec::kAlarmSize,
    "Alarm is no longer 7 packed bytes. The UDP payload is parsed off-board at "
    "fixed offsets -- update serialize_alarm() and the receiver together.");

static_assert(
    sizeof(daly100_bms::Daly100Bms::BmsData) == 385,
    "BmsData layout changed. serialize_data() enumerates its fields by hand, so a "
    "new or reordered field silently truncates or reshuffles the UDP payload. "
    "Update serialize_data() and the off-board receiver, then update this size.");

} // namespace telemetry

#endif // BMS_TELEMETRY_CODEC_HPP
