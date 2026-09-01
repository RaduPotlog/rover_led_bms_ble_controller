/// @file test_main.cpp
/// @brief Host-side unit tests for the Daly 100 BMS protocol decoder.
///
/// The decoder is pure byte-shuffling with no hardware dependency, so it runs on
/// the host against a stub Arduino.h. Frames are allocated on the heap at exactly
/// their real 13-byte size so AddressSanitizer catches any read past the end --
/// that is how the 0x97 out-of-bounds read is caught rather than merely argued.
///
/// Deliberately free of any test framework: the same file builds under
/// `pio test -e native` (test_framework = custom) and under a direct compiler
/// invocation, which matters because a host GCC is not present everywhere.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

#include "daly_100_bms.hpp"
#include "bms_telemetry_codec.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;
const char *g_current_test = "";

void fail(int line, const char *expr)
{
    ++g_failures;
    printf("  FAIL  %s:%d  %s\n", g_current_test, line, expr);
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) fail(__LINE__, #cond);                                    \
    } while (0)

#define CHECK_EQ(expected, actual)                                             \
    do {                                                                       \
        ++g_checks;                                                            \
        const long long e_ = (long long)(expected);                            \
        const long long a_ = (long long)(actual);                              \
        if (e_ != a_) {                                                        \
            ++g_failures;                                                      \
            printf("  FAIL  %s:%d  %s: expected %lld, got %lld\n",             \
                   g_current_test, __LINE__, #actual, e_, a_);                 \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(expected, actual, tol)                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        const double e_ = (double)(expected);                                  \
        const double a_ = (double)(actual);                                    \
        if (std::fabs(e_ - a_) > (tol)) {                                      \
            ++g_failures;                                                      \
            printf("  FAIL  %s:%d  %s: expected %f, got %f\n",                 \
                   g_current_test, __LINE__, #actual, e_, a_);                 \
        }                                                                      \
    } while (0)

/* -------------------------------------------------------------------------- */

/// @brief Build a 13-byte Daly frame with a correct trailing checksum.
/// @note Returned on the heap at exactly 13 bytes so ASan bounds are exact.
uint8_t *make_frame(uint8_t cmd, const uint8_t payload[8], uint8_t addr = 0x01)
{
    uint8_t *f = new uint8_t[13];

    f[0] = 0xA5;
    f[1] = addr;
    f[2] = cmd;
    f[3] = 0x08;
    memcpy(f + 4, payload, 8);

    uint8_t sum = 0;
    for (int i = 0; i < 12; ++i) sum += f[i];
    f[12] = sum;

    return f;
}

/// @brief Feed one frame through the decoder and free it.
void feed_frame(daly100_bms::Daly100Bms &bms, uint8_t cmd, const uint8_t payload[8])
{
    uint8_t *f = make_frame(cmd, payload);
    bms.decode_response(f);
    delete[] f;
}

/// @brief Report the pack cell count via 0x94 so cell assembly is configured.
void announce_cell_count(daly100_bms::Daly100Bms &bms, uint8_t cells, uint8_t temp_sensors = 1)
{
    const uint8_t p[8] = { cells, temp_sensors, 0x01, 0x01, 0x00, 0x00, 0x2A, 0x00 };
    feed_frame(bms, 0x94, p);
}

/* --- tests ---------------------------------------------------------------- */

void test_create_request_checksum()
{
    daly100_bms::Daly100Bms bms;
    uint8_t req[13];

    bms.create_request(0x90, req);

    CHECK_EQ(0xA5, req[0]);
    CHECK_EQ(0x40, req[1]);
    CHECK_EQ(0x90, req[2]);
    CHECK_EQ(0x08, req[3]);

    for (int i = 4; i < 12; ++i) CHECK_EQ(0x00, req[i]);

    // 0xA5 + 0x40 + 0x90 + 0x08 = 0x17D, truncated to one byte.
    CHECK_EQ(0x7D, req[12]);
}

void test_fresh_decoder_reads_zero()
{
    // Not academic: a decoder whose state is not zeroed reports garbage for every
    // field until a frame carrying that field arrives, and the telemetry publisher
    // serialises the whole struct every cycle regardless.
    daly100_bms::Daly100Bms bms;

    CHECK_NEAR(0.0, bms.get_pack_voltage(), 0.001);
    CHECK_NEAR(0.0, bms.get_pack_current(), 0.001);
    CHECK_NEAR(0.0, bms.get_pack_soc(), 0.001);
    CHECK_EQ(0, bms.get_charge_discharge_status());
    CHECK(!bms.get_charge_fet_state());
    CHECK(!bms.get_discharge_fet_state());

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    for (int i = 0; i < daly100_bms::Daly100Bms::kMaxCells; ++i) {
        CHECK_NEAR(0.0, d->cellVmV[i], 0.001);
        CHECK(!d->cellBalanceState[i]);
    }

    CHECK(!bms.get_alarm()->levelOneCellVoltageTooHigh);
}

void test_rejects_bad_checksum()
{
    daly100_bms::Daly100Bms bms;

    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);
    f[12] ^= 0xFF; // corrupt the checksum

    bms.decode_response(f);
    delete[] f;

    // Nothing should have been written.
    CHECK_NEAR(0.0, bms.get_pack_voltage(), 0.001);
}

void test_0x90_pack_measurements()
{
    daly100_bms::Daly100Bms bms;

    // 53.2 V; current raw 29950 -> (29950 - 30000)/10 = -5.0 A; SOC 85.5 %.
    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    feed_frame(bms, 0x90, p);

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
    CHECK_NEAR(-5.0, bms.get_pack_current(), 0.001);
    CHECK_NEAR(85.5, bms.get_pack_soc(), 0.001);
}

void test_0x91_min_max_cell_voltage()
{
    daly100_bms::Daly100Bms bms;

    // max 3456 mV on cell 3, min 3401 mV on cell 7.
    const uint8_t p[8] = { 0x0D, 0x80, 0x03, 0x0D, 0x49, 0x07, 0x00, 0x00 };
    feed_frame(bms, 0x91, p);

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    CHECK_NEAR(3456.0, d->maxCellmV, 0.001);
    CHECK_EQ(3, d->maxCellVNum);
    CHECK_NEAR(3401.0, d->minCellmV, 0.001);
    CHECK_EQ(7, d->minCellVNum);
    CHECK_NEAR(55.0, d->cellDiff, 0.001);
}

void test_0x92_temperature_offset()
{
    daly100_bms::Daly100Bms bms;

    // BMS adds a +40 offset: 65 -> 25 C, 58 -> 18 C.
    const uint8_t p[8] = { 65, 0x00, 58, 0x00, 0x00, 0x00, 0x00, 0x00 };
    feed_frame(bms, 0x92, p);

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    CHECK_NEAR(25.0, d->tempMax, 0.001);
    CHECK_NEAR(18.0, d->tempMin, 0.001);
    CHECK_NEAR(21.5, d->tempAverage, 0.001);
}

void test_0x93_mosfet_status()
{
    daly100_bms::Daly100Bms bms;

    // Discharging, both MOSFETs on, heartbeat 42, residual 12345 mAh.
    const uint8_t p[8] = { 0x02, 0x01, 0x01, 42, 0x00, 0x00, 0x30, 0x39 };
    feed_frame(bms, 0x93, p);

    CHECK_EQ(2, bms.get_charge_discharge_status());
    CHECK(bms.get_charge_fet_state());
    CHECK(bms.get_discharge_fet_state());
    CHECK_EQ(12345, bms.get_data()->resCapacitymAh);
}

void test_0x94_drives_expected_frame_count()
{
    daly100_bms::Daly100Bms bms;

    announce_cell_count(bms, 13);

    CHECK_EQ(13, bms.get_data()->numberOfCells);
    // 13 cells at 3 per frame needs 5 frames, not 4.
    CHECK_EQ(5, bms.get_expected_frame_count());

    announce_cell_count(bms, 12);
    CHECK_EQ(4, bms.get_expected_frame_count());
}

void test_0x95_assembles_all_twelve_cells()
{
    daly100_bms::Daly100Bms bms;

    announce_cell_count(bms, 12);

    // Four frames, three cells each. Cell i carries 3000 + i mV, so every cell
    // is distinct and a smeared or overwritten slot is immediately visible.
    for (int frame = 1; frame <= 4; ++frame) {
        uint8_t p[8] = { 0 };
        p[0] = (uint8_t)frame;

        for (int i = 0; i < 3; ++i) {
            const int cell = (frame - 1) * 3 + i;
            const uint16_t mv = (uint16_t)(3000 + cell);
            p[1 + 2 * i] = (uint8_t)(mv >> 8);
            p[2 + 2 * i] = (uint8_t)(mv & 0xFF);
        }

        feed_frame(bms, 0x95, p);
    }

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    for (int cell = 0; cell < 12; ++cell) {
        CHECK_NEAR(3000.0 + cell, d->cellVmV[cell], 0.001);
    }
}

void test_0x96_cell_temperatures()
{
    daly100_bms::Daly100Bms bms;

    announce_cell_count(bms, 12, 3);

    // Frame 1, three sensors: 25 C, 26 C, 27 C (with the +40 offset).
    const uint8_t p[8] = { 0x01, 65, 66, 67, 40, 40, 40, 40 };
    feed_frame(bms, 0x96, p);

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    CHECK_EQ(25, d->cellTemperature[0]);
    CHECK_EQ(26, d->cellTemperature[1]);
    CHECK_EQ(27, d->cellTemperature[2]);
}

void test_0x97_cell_balance_state()
{
    daly100_bms::Daly100Bms bms;

    announce_cell_count(bms, 12);

    // Balance bytes occupy payload[0..5], one bit per cell, LSB first.
    // Set cell 0 (byte 0 bit 0), cell 15 (byte 1 bit 7), cell 41 (byte 5 bit 1).
    uint8_t p[8] = { 0 };
    p[0] = 0x01;
    p[1] = 0x80;
    p[5] = 0x02;

    feed_frame(bms, 0x97, p);

    const daly100_bms::Daly100Bms::BmsData *d = bms.get_data();

    CHECK(d->cellBalanceState[0]);
    CHECK(d->cellBalanceState[15]);
    CHECK(d->cellBalanceState[41]);
    CHECK(!d->cellBalanceState[1]);
    CHECK(!d->cellBalanceState[14]);
    CHECK(!d->cellBalanceState[40]);
    CHECK(d->cellBalanceActive);
}

void test_0x98_failure_codes()
{
    daly100_bms::Daly100Bms bms;

    uint8_t p[8] = { 0 };
    p[0] = 0x01; // level one cell voltage too high
    p[2] = 0x08; // level two discharge current too high
    p[6] = 0x04; // failure of short circuit protection

    feed_frame(bms, 0x98, p);

    const daly100_bms::Daly100Bms::Alarm *a = bms.get_alarm();

    CHECK(a->levelOneCellVoltageTooHigh);
    CHECK(!a->levelTwoCellVoltageTooHigh);
    CHECK(a->levelTwoDischargeCurrentTooHigh);
    CHECK(a->failureOfShortCircuitProtection);
    CHECK(!a->failureOfLowVoltageNoCharging);
}

/* --- reassembly ----------------------------------------------------------- */

void test_feed_single_frame()
{
    daly100_bms::Daly100Bms bms;

    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);

    bms.feed(f, 13);
    delete[] f;

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
}

void test_feed_frame_split_across_calls()
{
    daly100_bms::Daly100Bms bms;

    // BLE notifications carry an arbitrary slice of the byte stream, so a frame
    // routinely straddles two of them.
    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);

    bms.feed(f, 5);
    CHECK_NEAR(0.0, bms.get_pack_voltage(), 0.001); // nothing decodable yet

    bms.feed(f + 5, 8);
    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);

    delete[] f;
}

void test_feed_skips_leading_garbage()
{
    daly100_bms::Daly100Bms bms;

    const uint8_t junk[4] = { 0x00, 0x11, 0x22, 0x33 };
    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);

    bms.feed(junk, sizeof(junk));
    bms.feed(f, 13);
    delete[] f;

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
}

void test_feed_survives_false_sync_byte()
{
    daly100_bms::Daly100Bms bms;

    // A 0xA5 inside a payload is a false sync point. Consuming 13 bytes from it
    // would swallow the real frame that follows; only the checksum distinguishes
    // them, so it has to be checked before the bytes are consumed.
    const uint8_t decoy[3] = { 0xA5, 0xA5, 0x00 };
    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);

    bms.feed(decoy, sizeof(decoy));
    bms.feed(f, 13);
    delete[] f;

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
}

void test_feed_decodes_back_to_back_frames()
{
    daly100_bms::Daly100Bms bms;

    const uint8_t p90[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    const uint8_t p92[8] = { 65, 0x00, 58, 0x00, 0x00, 0x00, 0x00, 0x00 };

    uint8_t *a = make_frame(0x90, p90);
    uint8_t *b = make_frame(0x92, p92);

    uint8_t joined[26];
    memcpy(joined, a, 13);
    memcpy(joined + 13, b, 13);

    delete[] a;
    delete[] b;

    bms.feed(joined, sizeof(joined));

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
    CHECK_NEAR(25.0, bms.get_data()->tempMax, 0.001);
}

void test_feed_oversized_burst_does_not_overflow()
{
    daly100_bms::Daly100Bms bms;

    // Far more than the reassembly buffer holds, ending in a good frame. ASan is
    // the real assertion here; the decode proves the tail survived the trim.
    const uint8_t p[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    uint8_t *f = make_frame(0x90, p);

    uint8_t flood[1024];
    memset(flood, 0xA5, sizeof(flood));
    memcpy(flood + sizeof(flood) - 13, f, 13);
    delete[] f;

    bms.feed(flood, sizeof(flood));

    CHECK_NEAR(53.2, bms.get_pack_voltage(), 0.001);
}

/* --- telemetry codec ------------------------------------------------------ */

void test_codec_packet_size_matches_structs()
{
    // The packed structs are the wire format. If these drift, the receiver reads
    // every field after the change at the wrong offset.
    CHECK_EQ(385, sizeof(daly100_bms::Daly100Bms::BmsData));
    CHECK_EQ(7, sizeof(daly100_bms::Daly100Bms::Alarm));
    CHECK_EQ(392, telemetry::BmsTelemetryCodec::kPacketSize);
}

void test_codec_serializes_full_packet()
{
    daly100_bms::Daly100Bms bms;

    const uint8_t p90[8] = { 0x02, 0x14, 0x00, 0x00, 0x74, 0xFE, 0x03, 0x57 };
    feed_frame(bms, 0x90, p90);

    uint8_t buf[telemetry::BmsTelemetryCodec::kPacketSize];
    memset(buf, 0xCC, sizeof(buf));

    const size_t written = telemetry::BmsTelemetryCodec::serialize(
        bms.get_data(), bms.get_alarm(), buf, sizeof(buf));

    // Every byte must be accounted for. A field added to BmsData but not to
    // serialize_data() shows up here as a short write.
    CHECK_EQ(telemetry::BmsTelemetryCodec::kPacketSize, written);

    // Pack voltage is the first field, little-endian float 53.2.
    float voltage = 0.0f;
    memcpy(&voltage, buf, sizeof(voltage));
    CHECK_NEAR(53.2, voltage, 0.001);
}

void test_codec_rejects_short_buffer()
{
    daly100_bms::Daly100Bms bms;

    uint8_t buf[8];
    const size_t written = telemetry::BmsTelemetryCodec::serialize(
        bms.get_data(), bms.get_alarm(), buf, sizeof(buf));

    CHECK_EQ(0, written);
}

void test_codec_alarm_bits_land_in_the_right_bytes()
{
    daly100_bms::Daly100Bms bms;

    uint8_t p[8] = { 0 };
    p[0] = 0x01; // byte 0 bit 0: level one cell voltage too high
    p[2] = 0x08; // byte 2 bit 3: level two discharge current too high
    p[6] = 0x04; // byte 6 bit 2: failure of short circuit protection
    feed_frame(bms, 0x98, p);

    uint8_t buf[telemetry::BmsTelemetryCodec::kPacketSize];
    const size_t written = telemetry::BmsTelemetryCodec::serialize(
        bms.get_data(), bms.get_alarm(), buf, sizeof(buf));

    CHECK_EQ(telemetry::BmsTelemetryCodec::kPacketSize, written);

    const uint8_t *alarm_bytes = buf + telemetry::BmsTelemetryCodec::kDataSize;

    CHECK_EQ(0x01, alarm_bytes[0]);
    CHECK_EQ(0x00, alarm_bytes[1]);
    CHECK_EQ(0x08, alarm_bytes[2]);
    CHECK_EQ(0x04, alarm_bytes[6]);
}

struct TestCase {
    const char *name;
    void (*fn)();
};

const TestCase kTests[] = {
    { "create_request_checksum",           test_create_request_checksum },
    { "fresh_decoder_reads_zero",          test_fresh_decoder_reads_zero },
    { "rejects_bad_checksum",              test_rejects_bad_checksum },
    { "0x90_pack_measurements",            test_0x90_pack_measurements },
    { "0x91_min_max_cell_voltage",         test_0x91_min_max_cell_voltage },
    { "0x92_temperature_offset",           test_0x92_temperature_offset },
    { "0x93_mosfet_status",                test_0x93_mosfet_status },
    { "0x94_drives_expected_frame_count",  test_0x94_drives_expected_frame_count },
    { "0x95_assembles_all_twelve_cells",   test_0x95_assembles_all_twelve_cells },
    { "0x96_cell_temperatures",            test_0x96_cell_temperatures },
    { "0x97_cell_balance_state",           test_0x97_cell_balance_state },
    { "0x98_failure_codes",                test_0x98_failure_codes },
    { "feed_single_frame",                 test_feed_single_frame },
    { "feed_frame_split_across_calls",     test_feed_frame_split_across_calls },
    { "feed_skips_leading_garbage",        test_feed_skips_leading_garbage },
    { "feed_survives_false_sync_byte",     test_feed_survives_false_sync_byte },
    { "feed_decodes_back_to_back_frames",  test_feed_decodes_back_to_back_frames },
    { "feed_oversized_burst_no_overflow",  test_feed_oversized_burst_does_not_overflow },
    { "codec_packet_size_matches_structs", test_codec_packet_size_matches_structs },
    { "codec_serializes_full_packet",      test_codec_serializes_full_packet },
    { "codec_rejects_short_buffer",        test_codec_rejects_short_buffer },
    { "codec_alarm_bits_placement",        test_codec_alarm_bits_land_in_the_right_bytes },
};

} // namespace

int main()
{
    const int count = (int)(sizeof(kTests) / sizeof(kTests[0]));
    int failed_tests = 0;

    for (int i = 0; i < count; ++i) {
        g_current_test = kTests[i].name;
        const int before = g_failures;

        kTests[i].fn();

        const bool ok = (g_failures == before);
        if (!ok) ++failed_tests;
        printf("%-4s %s\n", ok ? "ok" : "FAIL", kTests[i].name);
    }

    printf("\n%d tests, %d checks, %d failed checks, %d failed tests\n",
           count, g_checks, g_failures, failed_tests);

    return (g_failures == 0) ? 0 : 1;
}
