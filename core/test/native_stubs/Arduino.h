/// @file Arduino.h
/// @brief Minimal host-build stub for the Arduino core.
/// @note Only what daly_100_bms.{hpp,cpp} and logging.hpp actually need, so the
///       protocol decoder can be unit-tested off the board. Kept deliberately
///       small -- if a test needs more of the Arduino API than this, that is a
///       signal the code under test has grown a hardware dependency it should
///       not have. Selected via -I test/native_stubs in the native env.

#ifndef ROVER_NATIVE_STUB_ARDUINO_H
#define ROVER_NATIVE_STUB_ARDUINO_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>

#ifndef bitRead
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#endif

#endif // ROVER_NATIVE_STUB_ARDUINO_H
