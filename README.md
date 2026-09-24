# rover_led_controller

ESP32 firmware driving a UDP-fed APA102 LED strip for the rover.

It does one job: listen on a UDP port for colour frames and paint them onto the
strip, with a local red-blink / solid-blue link-status animation for when there
is nothing to paint.

This is the LED-only branch. `master` carries the combined `rover_controller`
firmware, which also runs a BLE client to a Daly 100 BMS, a Nextion HMI, and a
second UDP socket for BMS telemetry. Stripping those out leaves the 2.4 GHz radio
to WiFi alone — the BLE polling traffic on `master` adds jitter to UDP frame
timing — and leaves UART0 to the debug console alone.

## Configure before flashing

Deployment settings live in **`core/include/config.hpp`**. Network credentials
live in **`core/include/config_local.hpp`**, which is git-ignored — copy
`config_local.hpp.example` next to it and fill in your network:

```sh
cp core/include/config_local.hpp.example core/include/config_local.hpp
```

Without it the firmware still builds; it just will not associate.

The board claims a static `192.168.77.201/24` (`ROVER_WIFI_USE_STATIC_IP=1`). The
host running `rover_led` must be on that same network — the one the configured
SSID serves. Set `ROVER_WIFI_USE_STATIC_IP=0` to take an address over DHCP
instead; the LED socket only receives, so it needs no fixed address of its own.

Build flags in `platformio.ini` control the rest:

| Flag | Default | Effect |
|---|---|---|
| `ROVER_DEBUG` | `1` | `0` compiles logging out, `1` logs to `Serial`/UART0, `2` logs to `Serial1` on a separate debug UART |
| `ROVER_WIFI_USE_STATIC_IP` | `1` | `1` claims the static address block in `config.hpp` instead of using DHCP |
| `ROVER_NUM_LEDS` | `40` | Length of the LED strip. Every frame buffer, render loop and UDP frame size derives from it |
| `ROVER_LED_SELFTEST` | `1` | Boot-time strip diagnostic: `0` off, `1` chase then dim fill, `2` repeat one frame forever for a scope — see [Troubleshooting](#only-the-first-few-leds-light) |
| `ROVER_LED_CHIPSET` | `APA102` | FastLED chipset name. Pair with `ROVER_LED_CLOCKLESS` and `ROVER_LED_COLOR_ORDER` |
| `ROVER_LED_COLOR_ORDER` | `BGR` | Byte order the chipset expects. `GRB` for WS2812B |
| `ROVER_LED_CLOCKLESS` | `0` | `1` for a single-wire strip, which takes no clock pin |
| `ROVER_LED_SPI_MHZ` | `6` | Bit-banged clock rate for a clocked chipset. FastLED's own APA102 default, because long strips cannot hold a faster clock. Drop to `1` to rule out clock integrity |
| `ROVER_LED_BRIGHTNESS` | `255` | Global FastLED brightness. Lowering it tells a power limit from a data limit |
| `ROVER_LED_MAX_MILLIAMPS` | `0` | Current budget at 5 V; FastLED scales brightness to fit. `0` disables the cap. Off by default so it cannot mask an under-fed strip |

## Build and flash

```sh
cd core
pio run                         # build
pio run --target upload         # flash
pio device monitor -b 115200    # console
```

A healthy boot logs the startup banner, the WiFi banner with the board's IP, and
`UDP listening on port 3333`.

## Smoke test

There are no host unit tests on this branch — the ones on `master` cover only the
Daly protocol decoder and the BMS telemetry codec, neither of which is built
here. The frame path is exercised by driving the socket directly instead. From
any machine on the board's subnet:

```python
import socket, struct
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
frame = b'\x00\x00\x00\x00' + b''.join(struct.pack('<I', 0x00FF0000) for _ in range(40))
while True:
    s.sendto(frame, ('192.168.77.201', 3333))
```

The strip should go solid red and hold it. Stop the sender and it returns to
solid blue after about a second, logging `LED frames timed out, showing no-data
state`. Send a short frame — 10 colours instead of 40 — and the first 10 LEDs
paint while the rest go dark.

## Layout

```
core/
├── include/
│   ├── config.hpp              deployment settings — edit this
│   ├── config_local.hpp        network credentials — git-ignored
│   ├── logging.hpp             ROVER_LOG* macros and their sink
│   ├── connector_interface.hpp a link: is_connected() + poll()
│   ├── wifi_connector.hpp      WiFi station (singleton), optional static IP
│   ├── udp_connection.hpp      one socket per port; one is instantiated
│   └── led_controller.hpp      FastLED APA102 strip
├── src/                        matching implementations + main.cpp
└── platformio.ini
```

## Concurrency

Two tasks run concurrently: the Arduino loop task and the `async_udp` task
(incoming packets). Exactly one handoff crosses a task boundary, and it is
explicit: the UDP callback copies a frame into `LedController`'s staging buffer
under a short critical section, and `LedController::poll()` is the only caller of
`FastLED.show()`, so the strip has one writer.

`loop()` does not block.

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
| LED strip (40 LEDs) | `kLedDataPin` 5, `kLedClockPin` 16 |
| Debug console | UART0 (GPIO 1 TX / 3 RX) @ 115200 |
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

Branched from the combined `rover_controller` firmware on `master`, which itself
superseded two separate projects — `rover_bms_ble_controller` and
`rover_led_udp_controller`. This branch keeps only the LED half of that merge,
together with the connectivity layer (`ConnectorInterface`, `WifiConnector`,
`UdpConnection`) the two halves shared. The LED code's namespace stays
`led_controller`, as on `master`.
