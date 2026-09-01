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
  sent once per BMS poll cycle to a configured host.
- **UDP-driven LED strip** — a 24-LED APA102 strip taking its colours from
  incoming UDP frames, with a local red-blink / blue-flash link-status animation
  while WiFi is down.

## Configure before flashing

Deployment settings live in **`core/include/config.hpp`**. Network credentials
live in **`core/include/config_local.hpp`**, which is git-ignored — copy
`config_local.hpp.example` next to it and fill in your network:

```sh
cp core/include/config_local.hpp.example core/include/config_local.hpp
```

Without it the firmware still builds; it just will not associate. Also set
`bms_udp_dest_ip()` and `kBmsMacAddress` in `config.hpp` to match your deployment.

> **Subnet warning.** The two source projects targeted different networks, and
> their addresses were never mutually reachable: BMS telemetry goes to
> `192.168.1.201`, the LED board's static address was `192.168.99.101/24`, and the
> configured SSID hands out a third range over DHCP. On one board these must
> agree. Until they do, telemetry leaves the board and is dropped by the first
> router. `config.hpp` lists them together so the mismatch is visible.

Three build flags in `platformio.ini` control the rest:

| Flag | Default | Effect |
|---|---|---|
| `ROVER_DEBUG` | `1` | `0` compiles logging out (keeps the Nextion UART clean), `1` logs to `Serial`/UART0 alongside the display, `2` logs to `Serial1` on a separate debug UART |
| `ROVER_DEBUG_BMS` | `0` | `1` adds a per-frame dump of every decoded BMS response. Chatty: nine frames per poll cycle. Requires `ROVER_DEBUG` to be non-zero |
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

## Tests

The Daly protocol decoder and the telemetry codec are pure byte-shuffling with no
hardware dependency, so they are unit-tested on the host against a stub
`Arduino.h` in `core/test/native_stubs/`. AddressSanitizer is on, because the
decoder indexes into fixed-size frame payloads and a bounds slip should fail the
build rather than the pack.

```sh
cd core
pio test -e native
```

That needs a host GCC on `PATH`. On a Windows box with Visual Studio but no GCC,
`tools/run_native_tests.ps1` builds and runs the same sources with `cl.exe`:

```sh
pwsh -File tools/run_native_tests.ps1
```

## Layout

```
core/
├── include/
│   ├── config.hpp              deployment settings — edit this
│   ├── config_local.hpp        network credentials — git-ignored
│   ├── logging.hpp             ROVER_LOG* macros and their sink
│   ├── connector_interface.hpp a link: is_connected() + poll()
│   ├── wifi_connector.hpp      WiFi station (singleton), optional static IP
│   ├── udp_connection.hpp      one socket per port; two are instantiated
│   ├── ble_scanner.hpp         NimBLE scan for the configured BMS
│   ├── ble_client.hpp          connect, subscribe, reconnect
│   ├── daly_100_bms.hpp        Daly protocol: reassembly, encode, decode
│   ├── daly_bms_poller.hpp     non-blocking command cycle
│   ├── bms_telemetry_codec.hpp the outbound UDP payload
│   ├── nextion_view.hpp        display rendering, one field at a time
│   └── led_controller.hpp      FastLED APA102 strip
├── src/                        matching implementations + main.cpp
├── test/                       host unit tests and their Arduino stub
├── tools/                      MSVC fallback test runner
└── platformio.ini
resources/                      Nextion .HMI project and its fonts
```

`UdpConnection` is instantiated twice — once on port 4444 for telemetry out, once
on port 3333 for LED frames in. Each holds its own `AsyncUDP`, so no coordination
is needed between them.

## Concurrency

Three tasks run concurrently: the Arduino loop task, the NimBLE host task
(BLE notifications) and the `async_udp` task (incoming packets). Exactly two
handoffs cross a task boundary, and both are explicit:

- **BLE bytes.** The notification callback pushes raw bytes into a FreeRTOS stream
  buffer and returns. `DalyBmsPoller::poll()` drains it on the loop task and feeds
  the decoder there, so decoded BMS state has a single reader and writer and needs
  no lock.
- **LED frames.** The UDP callback copies a frame into a staging buffer under a
  short critical section. `LedController::poll()` is the only caller of
  `FastLED.show()`, so the strip has one writer.

`loop()` does not block. The BMS command cycle issues one command per iteration
once `kBmsCommandIntervalMs` has elapsed — the cycle still takes about 900 ms, but
nothing waits on it — and the display writes at most one changed field per
iteration, gated on the UART having room.

### LED strip ownership

While WiFi is down, `LedController` is in `Mode::LinkStatus` and blinks the strip
red on a 2 s period. On the first successful connect `loop()` switches it to
`Mode::Udp`, which flashes blue once and then shows incoming frames. If the link
drops it goes back. Frames that arrive in the wrong mode are dropped rather than
queued, since they would be stale by the time the mode changed.

Incoming LED frames must be at least 100 bytes: a 4-byte header followed by
24 × `uint32_t` colours, low 24 bits used, BGR order.

## Hardware

| Function | Pins |
|---|---|
| Nextion display | UART0 (GPIO 1 TX / 3 RX) @ 9600 |
| APA102 strip | `kLedDataPin` 5, `kLedClockPin` 16 |
| Debug UART (`ROVER_DEBUG=2` only) | `ROVER_LOG_TX_PIN` 17, RX unassigned |

The debug UART's RX pin is deliberately `-1`: logging only ever transmits, and
claiming GPIO 16 for a receiver nothing reads would collide with the APA102 clock.

WiFi and BLE share the 2.4 GHz radio. The BLE polling traffic adds jitter to UDP
LED frame timing; this is inherent to running both on one chip.

## Provenance

Superseded `rover_bms_ble_controller` and `rover_led_udp_controller`, which each
carried a near-identical private copy of `ConnectorInterface`, `WifiConnector`,
and `UdpConnection`. Those copies are now unified here. The LED code moved from
namespace `rover_led_controller` to `led_controller`, matching the existing
`daly100_bms` convention; connectivity and BLE remain in `connector`.
