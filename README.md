# rover_controller

Combined ESP32 firmware for the rover, merging what were two separate PlatformIO
projects (`rover_bms_ble_controller` and `rover_led_udp_controller`) into a single
binary running on one board.

It does four jobs at once:

- **BMS over BLE** — NimBLE client to a Daly 100 BMS, polling nine commands
  (`0x90`–`0x98`) and decoding the 13-byte `0xA5` frame stream.
- **Nextion HMI** — pack voltage, current, temperature, SOC, charge/discharge
  state, and WiFi/BLE/MOSFET status icons on UART0.
- **UDP telemetry out** — serialized `BmsData` + a 7-byte packed `Alarm` bitfield,
  sent once per loop to a configured host.
- **UDP-driven LED strip** — a 24-LED APA102 strip taking its colours from
  incoming UDP frames, with a local red-blink / blue-flash link-status animation
  while WiFi is down.

## Configure before flashing

Everything deployment-specific lives in **`core/include/config.hpp`**. At minimum
set `kWifiSsid`, `kWifiPass`, `kBmsUdpDestIp`, and `BMS_MAC_ADDRESS`.

> **Subnet warning.** The two source projects targeted different networks, and
> their addresses were never mutually reachable: BMS telemetry went to
> `192.168.1.201`, the LED board sat on a static `192.168.99.101/24`, and the BMS
> board took a DHCP lease on a third network. On one board these must agree.
> `config.hpp` lists them together so the mismatch is visible — reconcile them
> before deploying.

Two build flags in `platformio.ini` control the rest:

| Flag | Default | Effect |
|---|---|---|
| `ROVER_DEBUG` | `1` | `0` compiles logging out (keeps the Nextion UART clean), `1` logs to `Serial`/UART0 alongside the display, `2` logs to `Serial1` on a separate debug UART |
| `ROVER_WIFI_USE_STATIC_IP` | `0` | `1` claims the static address block in `config.hpp` instead of using DHCP |

### A note on UART0

The Nextion display and the debug console share UART0 at **9600 baud**. This is
inherited behaviour — the display ignores anything not terminated by
`0xFF 0xFF 0xFF`, so debug text and display commands coexist, noisily. Set
`ROVER_DEBUG=0` for a clean display line, or `ROVER_DEBUG=2` to move logs onto
their own UART.

## Build and flash

```sh
cd core
pio run                       # build
pio run --target upload       # flash
pio device monitor -b 9600    # console
```

A healthy boot logs the WiFi banner and IP, `UDP listening on port 4444`,
`UDP listening on port 3333`, and `Subscription success` once the BLE client has
attached to the BMS.

## Layout

```
core/
├── include/
│   ├── config.hpp              deployment settings — edit this
│   ├── logging.hpp             ROVER_LOG* macros and their sink
│   ├── connector_interface.hpp abstract link
│   ├── wifi_connector.hpp      WiFi station (singleton), optional static IP
│   ├── udp_connection.hpp      one socket per port; two are instantiated
│   ├── ble_connection.hpp      NimBLE scan, connect, subscribe
│   ├── daly_100_bms.hpp        Daly protocol encode/decode
│   └── led_controller.hpp      FastLED APA102 strip
├── src/                        matching implementations + main.cpp
└── platformio.ini
resources/                      Nextion .HMI project and its fonts
```

`UdpConnection` is instantiated twice — once on port 4444 for telemetry out, once
on port 3333 for LED frames in. Each holds its own `AsyncUDP`, so no coordination
is needed between them.

### LED strip ownership

While WiFi is down, `loop()` owns the strip and blinks it red on a 2 s period. On
the first successful connect it flashes blue once and hands ownership to the UDP
receive callback, which then drives every frame. If the link drops, `loop()` takes
ownership back. The `gIsLedUdpOwner` flag enforces this so the two writers never
fight over the strip.

Incoming LED frames must be at least 104 bytes: a 4-byte header followed by
24 × `uint32_t` colours, low 24 bits used, BGR order.

## Hardware

| Function | Pins |
|---|---|
| Nextion display | UART0 (GPIO 1 TX / 3 RX) @ 9600 |
| APA102 strip | `DATA_PIN` 13, `CLOCK_PIN` 14 |
| Debug UART (`ROVER_DEBUG=2` only) | `ROVER_LOG_RX_PIN` 16, `ROVER_LOG_TX_PIN` 17 |

WiFi and BLE share the 2.4 GHz radio. The ~1 s BLE polling burst adds jitter to
UDP LED frame timing; this is inherent to running both on one chip.

## Provenance

Superseded `rover_bms_ble_controller` and `rover_led_udp_controller`, which each
carried a near-identical private copy of `ConnectorInterface`, `WifiConnector`,
and `UdpConnection`. Those copies are now unified here. The LED code moved from
namespace `rover_led_controller` to `led_controller`, matching the existing
`daly100_bms` convention; connectivity and BLE remain in `connector`.
