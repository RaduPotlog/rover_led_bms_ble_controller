#ifndef NEXTION_VIEW_HPP
#define NEXTION_VIEW_HPP

#include <Arduino.h>

#include "EasyNextionLibrary.h"

#include "config.hpp"
#include "daly_100_bms.hpp"

namespace ui
{

/// @brief Renders BMS state onto the Nextion HMI.
///
/// @note The display shares UART0 at 9600 baud, where a single field update is
///       around 30 bytes and takes roughly 30 ms to clock out. main.cpp used to
///       write all nine fields every iteration with a delay(10) between them,
///       which both stalled loop() and pushed far more traffic than the values
///       changed.
///
///       This keeps a shadow copy of what the display is currently showing and
///       writes at most one changed field per poll(), and only when the UART
///       transmit buffer has room. Unchanged fields cost nothing.
class NextionView
{

public:

    /// @brief Constructor.
    /// @param display The Nextion driver.
    /// @param port The UART the display is on, used to check for transmit room.
    NextionView(EasyNex &display, HardwareSerial &port);

    /// @brief Select the main page and mark every field for an initial write.
    void begin();

    /// @brief Recompute what the display should show.
    /// @param data Decoded BMS state.
    /// @param wifi_up Whether the WiFi link is up.
    /// @param ble_up Whether the BLE link is up.
    /// @note Cheap and side-effect free on the UART; call it as often as you like.
    void update(const daly100_bms::Daly100Bms::BmsData &data, bool wifi_up, bool ble_up);

    /// @brief Write at most one pending field to the display.
    void poll();

private:

    /// @brief Longest rendered value, including the unit suffix and terminator.
    static constexpr size_t kMaxTextLen = 24;

    /// @brief A field holding a string.
    struct TextField
    {
        const char *name;
        char desired[kMaxTextLen];
        char shown[kMaxTextLen];
        bool dirty;
    };

    /// @brief A field holding a number, used for the icon picture indices.
    struct NumField
    {
        const char *name;
        int desired;
        int shown;
        bool dirty;
    };

    /// @brief Set a text field's desired value, marking it dirty if it changed.
    void set_text(TextField &field, const char *value);

    /// @brief Set a numeric field's desired value, marking it dirty if it changed.
    void set_num(NumField &field, int value);

    /// @brief The Nextion driver.
    EasyNex &display_;

    /// @brief The UART the display is on.
    HardwareSerial &port_;

    /// @brief Text fields, in round-robin order.
    TextField text_[5];

    /// @brief Numeric fields, in round-robin order.
    NumField num_[4];

    /// @brief Next field to consider writing.
    size_t cursor_{0};

    /// @brief Whether begin() has run.
    bool started_{false};
};

} // namespace ui

#endif // NEXTION_VIEW_HPP
