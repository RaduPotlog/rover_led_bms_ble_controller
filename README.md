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

> **Subnet warning.** The board claims a static `192.168.77.201/24`
> (`ROVER_WIFI_USE_STATIC_IP=1`), but BMS telemetry still goes to
> `192.168.1.201` — not a reachable address from that subnet. Both must sit on the
> network the configured SSID serves. Point `bms_udp_dest_ip()` at the telemetry
> host's `192.168.77.0/24` address (not `.201`, which the board itself now holds).
> Until then telemetry leaves the board and is dropped by the first router.
> `config.hpp` lists them together so the mismatch is visible.

Build flags in `platformio.ini` control the rest:

| Flag | Default | Effect |
|---|---|---|
| `ROVER_DEBUG` | `1` | `0` compiles logging out (keeps the Nextion UART clean), `1` logs to `Serial`/UART0 alongside the display, `2` logs to `Serial1` on a separate debug UART |
| `ROVER_DEBUG_BMS` | `0` | `1` adds a per-frame dump of every decoded BMS response. Chatty: nine frames per poll cycle. Requires `ROVER_DEBUG` to be non-zero |
| `ROVER_WIFI_USE_STATIC_IP` | `1` | `1` claims the static address block in `config.hpp` instead of using DHCP |
| `ROVER_NUM_LEDS` | `40` | Length of the LED strip. Every frame buffer, render loop and UDP frame size derives from it |
| `ROVER_LED_SELFTEST` | `1` | Boot-time strip diagnostic: `0` off, `1` chase then dim fill, `2` repeat one frame forever for a scope — see [Troubleshooting](#only-the-first-few-leds-light) |
| `ROVER_LED_CHIPSET` | `APA102` | FastLED chipset name. Pair with `ROVER_LED_CLOCKLESS` and `ROVER_LED_COLOR_ORDER` |
| `ROVER_LED_COLOR_ORDER` | `BGR` | Byte order the chipset expects. `GRB` for WS2812B |
| `ROVER_LED_CLOCKLESS` | `0` | `1` for a single-wire strip, which takes no clock pin |
| `ROVER_LED_SPI_MHZ` | `6` | Bit-banged clock rate for a clocked chipset. FastLED's own APA102 default, because long strips cannot hold a faster clock. Drop to `1` to rule out clock integrity |
| `ROVER_LED_BRIGHTNESS` | `255` | Global FastLED brightness. Lowering it tells a power limit from a data limit |
| `ROVER_LED_MAX_MILLIAMPS` | `0` | Current budget at 5 V; FastLED scales brightness to fit. `0` disables the cap. Off by default so it cannot mask an under-fed strip |

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
`Mode::Udp`, which shows solid blue until frames arrive and then shows them. If the
link drops it goes back. Frames that arrive in the wrong mode are dropped rather
than queued, since they would be stale by the time the mode changed.

If no frame arrives for `kLedFrameTimeoutMs` (1 s) while in `Mode::Udp`, the strip
goes back to solid blue until frames resume. The WiFi disconnect event alone
cannot be relied on for this: it fires only after the beacon timeout when the AP
vanishes, and never when the sender stops or the ROS host drops off the network
while the board stays associated. The strip therefore never holds a frozen frame.

A lost BMS (BLE) link is deliberately not shown by this firmware, so a flaky BMS
never hides the rover's signal animations. The telemetry socket sends the no-data
payload instead; `rover_battery`'s watchdog expires, and `rover_safety`'s LED tree
plays the error animation through `rover_led`.

An incoming LED frame is a 4-byte header followed by one `uint32_t` per LED, low
24 bits used, in `ROVER_LED_COLOR_ORDER`. At the default `ROVER_NUM_LEDS` of 40
that is 164 bytes, but the length is not required to match: a frame carrying
fewer colours paints as far as it reaches and leaves the rest of the strip dark,
and colours past the end of the strip are ignored. Only a packet too short to
carry even one colour is rejected. Requiring an exact length instead would mean
every change to `ROVER_NUM_LEDS` silently dropped every frame from a sender that
had not been changed to match, freezing the strip on its last colour with nothing
to say why.

## Hardware

| Function | Pins |
|---|---|
| Nextion display | UART0 (GPIO 1 TX / 3 RX) @ 9600 |
| LED strip (40 LEDs) | `kLedDataPin` 5, `kLedClockPin` 16 |
| Debug UART (`ROVER_DEBUG=2` only) | `ROVER_LOG_TX_PIN` 17, RX unassigned |

The debug UART's RX pin is deliberately `-1`: logging only ever transmits, and
claiming GPIO 16 for a receiver nothing reads would collide with the strip clock.

The default strip is **APA102** — a clocked chipset, so it uses both pins and the
strip has four pads: 5V, GND, DI, CI.

FastLED bit-bangs the clock here whatever pins you pick: this build never defines
`FASTLED_ALL_PINS_HARDWARE_SPI`, so on ESP32 its `SPIOutput` resolves to the
software implementation for every pin. `ROVER_LED_SPI_MHZ` is not a literal
frequency either — `DATA_RATE_MHZ(X)` expands to a cycles-per-bit divider,
`(F_CPU / 1000000) / X`, and the achieved rate runs somewhat below nominal because
the GPIO writes themselves are not counted. Its default of 6 is FastLED's own
default for APA102, chosen because long strips cannot hold a faster clock.

**The strip needs its own 5 V supply.** An APA102 draws up to ~60 mA at full
white, so 40 of them want ~2.4 A — an order of magnitude more than a dev board's
5 V/VIN pin will pass. Tie that supply's ground to the ESP32's ground, and inject
5 V at the far end of the strip as well as the head. A 5 V-powered APA102 also
wants roughly 0.7 × VDD ≈ 3.5 V for a logic high, above the ESP32's 3.3 V output;
a level shifter (74AHCT125 or similar) on DI and CI removes that margin problem.

WiFi and BLE share the 2.4 GHz radio. The BLE polling traffic adds jitter to UDP
LED frame timing; this is inherent to running both on one chip.

## Troubleshooting

### Only the first few LEDs light

The firmware always paints the whole strip, so a strip that lights only part way
is either configured for the wrong length or failing downstream of the ESP32.

**The rule that settles most cases:** every APA102 pixel re-drives CO and DO for
the next one. So find the last lit pixel and look at its *output* pads. If it
lights but drives neither clock nor data onward, that pixel is under-powered or
faulty, and the strip will always end exactly there — no firmware setting moves
it. If instead its outputs are clean and the next pixel is simply dark, the fault
is further down the strip.

Work through it in this order.

**1. Check the length.** `ROVER_NUM_LEDS` in `platformio.ini` must match the
strip. Nothing addresses LEDs past it.

**2. Check power — this is usually it.** 40 APA102s at full white want ~2.4 A,
far beyond a USB port or a dev board's 5 V pin. Under-fed pixels stop re-driving
their outputs, so the strip dies at whatever pixel the voltage sags too far, and
that cutoff *wanders by an LED or two between boots* — a fixed cutoff every time
means a broken line or a dead pixel instead, not power.

Confirm it with a meter: probe 5V-to-GND at the first pixel, at the last lit one,
and at the far end while the strip is lit. Sagging toward ~4.2 V is the answer.
As a cross-check, lower `ROVER_LED_BRIGHTNESS` (try `64`) — if the whole strip
lights dim but only part of it lights bright, the supply is browning out.

The fix is a supply rated for the strip, a common ground, and 5 V injected at the
far end. If that genuinely is not available, `-D ROVER_LED_MAX_MILLIAMPS=500` lets
FastLED scale brightness to fit the budget so all the LEDs light dim rather than a
few lighting bright. That is a mitigation, not a fix, which is why it is off by
default — left on, it hides the fault it is compensating for.

**3. Check the strip type.** Count the pads:

| Pads | Type | Settings |
|---|---|---|
| 4 — 5V, GND, DI, CI | Clocked (APA102 / SK9822 / DotStar) | The defaults |
| 3 — 5V, GND, DIN | Single-wire (WS2812B / SK6812 / NeoPixel) | `-D ROVER_LED_CLOCKLESS=1 -D ROVER_LED_CHIPSET=WS2812B -D ROVER_LED_COLOR_ORDER=GRB` |

Feeding a single-wire strip a clocked bitstream typically lights only a handful
of leading pixels, which looks exactly like a length or power problem.

**4. Run the self-test.** Build with `-D ROVER_LED_SELFTEST=1` and watch the
strip at boot. It runs two phases and each answers a different question:

| What you see | What it means |
|---|---|
| The chase dot walks the whole strip | Data and clock reach the far end. Any LEDs still dark in normal operation are a power problem — step 2 |
| The chase dot stops part way | The bitstream is not arriving past that index. Wrong chipset (step 3), a broken data or clock line, an unpowered or dead pixel there (step 2), or too fast a clock (step 5) |
| The chase reaches the end but the dim fill does not | Power. The chase lights one LED at a time; the fill lights them all |

The chase draws roughly one pixel's current no matter how long the strip is, so
it isolates the data path from the supply.

**5. Check the clock, last.** Retry with `-D ROVER_LED_SPI_MHZ=1`. If the strip
gets no further than it did at the default 6, the clock rate is not your problem
and you should go back to step 2.

To look at the signal directly, build with `-D ROVER_LED_SELFTEST=2`. That repeats
one frame forever on a fixed cadence and never reaches `loop()`, so the bitstream
is periodic, a scope triggers on it cleanly, and neither WiFi nor UDP frames
disturb the strip while you probe CI and DI at any pixel.

## Provenance

Superseded `rover_bms_ble_controller` and `rover_led_udp_controller`, which each
carried a near-identical private copy of `ConnectorInterface`, `WifiConnector`,
and `UdpConnection`. Those copies are now unified here. The LED code moved from
namespace `rover_led_controller` to `led_controller`, matching the existing
`daly100_bms` convention; connectivity and BLE remain in `connector`.
