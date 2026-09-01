#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <Arduino.h>

/// @brief Debug logging sink.
/// @note   0 -- logging compiled out entirely. Use this to keep UART0 clean for
///              the Nextion display, which shares that port.
///         1 -- log to Serial (UART0). This is the default and matches the
///              behaviour of the original BMS firmware, where debug text and
///              Nextion commands shared one line. The display ignores anything
///              not terminated by 0xFF 0xFF 0xFF, so the two coexist, noisily.
///         2 -- log to Serial1 on a separate debug UART, leaving UART0 for the
///              display alone. Requires wiring; see ROVER_LOG_RX/TX_PIN below.
#ifndef ROVER_DEBUG
#define ROVER_DEBUG 1
#endif

#if ROVER_DEBUG == 2
#ifndef ROVER_LOG_RX_PIN
#define ROVER_LOG_RX_PIN 16
#endif
#ifndef ROVER_LOG_TX_PIN
#define ROVER_LOG_TX_PIN 17
#endif
#ifndef ROVER_LOG_BAUD
#define ROVER_LOG_BAUD 115200
#endif
#endif

#if ROVER_DEBUG == 1

#define ROVER_LOG_BEGIN()   do { } while (0)
#define ROVER_LOG(x)        Serial.print(x)
#define ROVER_LOGLN(x)      Serial.println(x)
#define ROVER_LOGF(...)     Serial.printf(__VA_ARGS__)

#elif ROVER_DEBUG == 2

#define ROVER_LOG_BEGIN()   Serial1.begin(ROVER_LOG_BAUD, SERIAL_8N1, ROVER_LOG_RX_PIN, ROVER_LOG_TX_PIN)
#define ROVER_LOG(x)        Serial1.print(x)
#define ROVER_LOGLN(x)      Serial1.println(x)
#define ROVER_LOGF(...)     Serial1.printf(__VA_ARGS__)

#else

#define ROVER_LOG_BEGIN()   do { } while (0)
#define ROVER_LOG(x)        do { (void)sizeof(x); } while (0)
#define ROVER_LOGLN(x)      do { (void)sizeof(x); } while (0)
#define ROVER_LOGF(...)     do { } while (0)

#endif // ROVER_DEBUG

#endif // LOGGING_HPP
