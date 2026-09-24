#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <Arduino.h>

/// @brief Debug logging sink.
/// @note   0 -- logging compiled out entirely.
///         1 -- log to Serial (UART0), which this firmware uses for nothing
///              else. This is the default.
///         2 -- log to Serial1 on a separate debug UART, leaving UART0 free.
///              Requires wiring; see ROVER_LOG_RX/TX_PIN below.
#ifndef ROVER_DEBUG
#define ROVER_DEBUG 1
#endif

#if ROVER_DEBUG == 2
/// @brief RX pin for the debug UART.
/// @note -1 leaves it unassigned. Logging only ever transmits, and claiming a pin
///       for a receiver nothing reads is how this collided with the APA102 clock
///       on GPIO 16. Set it to a real pin only if you add an input path.
#ifndef ROVER_LOG_RX_PIN
#define ROVER_LOG_RX_PIN -1
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
