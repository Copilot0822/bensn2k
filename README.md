# bensn2k

`bensn2k` is a work-in-progress marine electronics repository. It currently contains three Arduino sketches and one React dashboard:

- `NEWM5Stam/`: M5Stamp PLC firmware that samples battery voltage, receives wind angle packets over Wi-Fi UDP, displays status locally, and transmits selected values onto NMEA 2000.
- `WindWifiSenderESP32/`: XIAO ESP32C3 firmware that reads an AS5600 magnetic angle sensor and sends apparent wind angle packets to the M5Stamp PLC over Wi-Fi.
- `dueseatalk1/`: Arduino Due firmware that talks to a Raymarine SeaTalk 1 bus using 9-bit serial, receives and decodes SeaTalk datagrams, and can transmit autopilot keypress datagrams from USB serial commands.
- `marine-dashboard/`: a Vite/React browser dashboard for marine telemetry, layoutable widgets, and autopilot controls. It currently runs with mock data unless a firmware or server endpoint provides the expected HTTP API.

The most complete hardware data path in the repo today is:

```txt
AS5600 wind vane
    |
    | I2C
    v
XIAO ESP32C3 running WindWifiSenderESP32
    |
    | Wi-Fi station to M5Stamp PLC AP
    | UDP 192.168.4.1:20000, payload angle=<degrees>
    v
M5Stamp PLC running NEWM5Stam
    |
    | NMEA 2000 / CAN, PGN 130306
    v
Boat NMEA 2000 network
```

The M5Stamp PLC also samples two INA226 voltage monitors and sends those battery voltages to NMEA 2000:

```txt
INA226 on Port A + INA226 on Port C
    |
    | I2C, one port active at a time
    v
M5Stamp PLC running NEWM5Stam
    |
    | NMEA 2000 / CAN, PGN 127508
    v
Boat NMEA 2000 network
```

The SeaTalk 1 controller is currently a separate path:

```txt
Host computer or future bridge
    |
    | USB serial commands such as BTN:AUTO
    v
Arduino Due running dueseatalk1
    |
    | 9-bit 4800 baud SeaTalk 1 through external interface circuitry
    v
Raymarine SeaTalk 1 bus / autopilot control head
```

The React dashboard is designed around a future ESP32 or local HTTP bridge. Its UI already polls `GET /data` and posts autopilot commands to `POST /api/autopilot`, but none of the current Arduino sketches in this repo implement those HTTP endpoints yet.

## Repository layout

```txt
.
|-- NEWM5Stam/
|   |-- NEWM5Stam.ino
|   `-- build/esp32.esp32.m5stack_stamp_s3/
|       `-- Arduino build output and binaries
|-- WindWifiSenderESP32/
|   `-- WindWifiSenderESP32.ino
|-- dueseatalk1/
|   `-- dueseatalk1.ino
|-- marine-dashboard/
|   |-- README.md
|   |-- package.json
|   |-- vite.config.js
|   |-- src/
|   `-- public/
`-- .gitattributes
```

Notes:

- `NEWM5Stam/build/esp32.esp32.m5stack_stamp_s3/` is generated Arduino build output. It is tracked in this repo right now and includes `.bin`, `.elf`, `.map`, partition, SDK config, and flash argument files.
- `marine-dashboard/node_modules/`, `marine-dashboard/dist/`, and Vite log files may exist locally, but they are ignored by `marine-dashboard/.gitignore`.
- There is a `marine-dashboard/README.md` with dashboard-specific run/build notes. This root README describes how all repo parts appear to fit together.

## System roles

### M5Stamp PLC firmware: `NEWM5Stam`

`NEWM5Stam/NEWM5Stam.ino` is the hub of the current NMEA 2000 side. It runs on an M5Stamp PLC / Stamp S3 target and does four main jobs:

1. Reads two INA226 voltage monitors.
2. Receives apparent wind angle from the remote ESP32 wind sender over Wi-Fi UDP.
3. Shows battery and wind status on the M5 display.
4. Publishes battery status and apparent wind angle to NMEA 2000 over the ESP32 TWAI/CAN peripheral.

Important constants and behavior:

- Serial baud: `115200`.
- Arduino build target observed in `build.options.json`: `esp32:esp32:m5stack_stamp_s3`.
- Wi-Fi mode: access point.
- AP base SSID: `M5StampPLC-Wind`.
- Actual AP SSID at runtime:
  - `M5StampPLC-Wind-CANOK` when NMEA 2000 / CAN opens successfully.
  - `M5StampPLC-Wind-CANFAIL` when CAN setup fails.
- AP password: `wind1234`.
- AP IP/gateway: `192.168.4.1`.
- AP subnet: `255.255.255.0`.
- UDP wind port: `20000`.
- Wind packet freshness window: `3000 ms`.
- Wind transmit period to NMEA 2000: `100 ms`.
- Battery sample period: `500 ms`.
- Battery transmit period to NMEA 2000: `1500 ms`.
- Display refresh period: `250 ms`.
- Wind offset step from buttons: `1.0 deg`.

Battery inputs:

| Logical input | I2C pins | INA226 address | NMEA battery instance |
| --- | --- | --- | --- |
| Port A | SDA `2`, SCL `1` | `0x41` | `0` |
| Port C | SDA `5`, SCL `4` | `0x41` | `1` |

The sketch uses a single `Wire` instance and switches it between Port A and Port C. Before rebinding the pins, it attempts I2C bus recovery and then calls `Wire.end()` so the ESP32 I2C driver does not keep talking on the previous pins.

INA226 checks and configuration:

- Manufacturer ID register `0xFE` must equal `0x5449`.
- Die ID register `0xFF` must equal `0x2260`.
- Config register `0x00` is written with `0x4527`.
- Calibration register `0x05` is written with `0x0800`.
- Bus voltage register `0x02` is read and converted with `raw * 0.00125`.

NMEA 2000 output:

| Data | NMEA 2000 PGN | Code path |
| --- | --- | --- |
| Battery voltage | `127508` Battery Status | `SetN2kPGN127508(...)` |
| Apparent wind angle | `130306` Wind Data | `SetN2kWindSpeed(...)` |

The wind PGN sends wind speed as `N2kDoubleNA` because this repo currently measures only angle. Apparent wind angle is converted from degrees to radians before being sent. If no recent wind packet has been received, the angle is sent as `N2kDoubleNA`.

NMEA 2000 identity details currently configured:

- Product model: `M5StampPLC Batt+Wind`.
- Software version: `1.1.0`.
- Hardware version: `1.1.0`.
- Serial code string: `M5STAMPLC-BAT-WIND`.
- Manufacturer information string: `Copilot0822`.
- Installation description 1: `Batt A/C + WiFi AWA`.
- Installation description 2: `PGN 127508 + 130306`.
- Device unique number: `130042`.
- Device function: `170`.
- Device class: `35`.
- Manufacturer code: `2046`.
- Preferred source address: `22`.
- Mode: `N2km_ListenAndNode`.

CAN/TWAI:

- TX pin: `STAMPLC_PIN_CAN_TX`.
- RX pin: `STAMPLC_PIN_CAN_RX`.
- Logged baud rate constant: `250000`.
- The firmware periodically prints TWAI state and error counters every `5000 ms`.

M5 display and buttons:

- Summary screen shows battery voltages, adjusted apparent wind angle, wind offset, Wi-Fi station count, and UDP port.
- Wind detail screen shows raw angle, adjusted angle, packet age, last sender IP, packet count, and offset.
- Button A increases wind offset by `1 deg`.
- Button B decreases wind offset by `1 deg`.
- Button C toggles between summary and wind detail screens.
- The offset is stored in ESP32 preferences namespace `windcfg`, key `offset_deg`.

PLC relay behavior:

- The sketch initializes the AW9523 I/O expander through `M5StamPLC`.
- Relay channels `0` and `1` are turned on during setup.
- Channels `0` through `3` are configured as outputs and set low before relay 1 and relay 2 are enabled.

### Wind sender firmware: `WindWifiSenderESP32`

`WindWifiSenderESP32/WindWifiSenderESP32.ino` runs on a Seeed Studio XIAO ESP32C3 or compatible ESP32C3 target. It reads an AS5600 magnetic rotary position sensor and sends the measured angle to the M5Stamp PLC over UDP.

Important constants and behavior:

- Serial baud: `115200`.
- I2C frequency: `100000`.
- Sample/send period: `100 ms`.
- Wi-Fi reconnect attempt period: `5000 ms`.
- Wi-Fi connect timeout per SSID: `15000 ms`.
- UDP destination IP: `192.168.4.1`.
- UDP destination port: `20000`.
- Packet format: `angle=<degrees with 2 decimals>`, for example `angle=123.45`.
- AP password: `wind1234`.

I2C wiring:

| ESP32C3 pin alias | GPIO | Purpose |
| --- | --- | --- |
| `D4` | GPIO `6` | AS5600 SDA |
| `D5` | GPIO `7` | AS5600 SCL |

AS5600 details:

- I2C address: `0x36`.
- Status register: `0x0B`.
- Raw angle high register: `0x0C`.
- Magnet detected mask: `0x20`.
- Counts per revolution: `4096`.
- Angle conversion: `rawAngle * (360 / 4096)`.

The sender checks for AS5600 communication and magnet presence before sending. It logs a status change immediately and otherwise repeats sensor status roughly every `5000 ms`.

Wi-Fi behavior:

The sender tries these access point names in order:

1. `M5StampPLC-Wind-CANOK`
2. `M5StampPLC-Wind-CANFAIL`
3. `M5StampPLC-Wind`

The M5Stamp PLC normally advertises one of the first two, depending on CAN state. The plain base SSID is included as a fallback candidate.

### SeaTalk 1 controller firmware: `dueseatalk1`

`dueseatalk1/dueseatalk1.ino` runs on an Arduino Due and implements a SeaTalk 1 controller interface using the SAM3X USART0 in 9-bit mode. It can listen to the bus, decode a few known datagrams, and transmit Raymarine-style keypress datagrams when commanded over USB serial.

Important constants and behavior:

- USB serial baud: `115200`.
- SeaTalk baud: `4800`.
- USART mode: 9-bit, 8 data bits plus command bit.
- SeaTalk TX is currently enabled with `ENABLE_SEATALK_TX 1`.
- Idle pin debug is currently enabled with `DEBUG_IDLE_PIN 1`.
- Expected RX idle polarity is high with `RX_IDLE_IS_HIGH 1`.
- SeaTalk idle requirement before TX: `2500 us`.
- SeaTalk TX idle wait timeout: `300 ms`.
- Datagram parser timeout: `15000 us`.
- Heartbeat status print period: `3000 ms`.

Hardware wiring described by the sketch:

| Arduino Due connection | Purpose |
| --- | --- |
| TX1 pin `18` / PA11 / TXD0 | SeaTalk transmit through SN74LS07 to SeaTalk DATA |
| RX1 pin `19` / PA10 / RXD0 | SeaTalk receive through non-inverting PC817 receiver |
| Digital pin `22` | Dedicated idle sense input wired to the same 3.3 V RX node |

Expected polarity:

- SeaTalk bus high means Due RX1 pin `19` high and idle sense pin `22` high.
- SeaTalk bus low means Due RX1 pin `19` low and idle sense pin `22` low.

USB serial commands:

| Command | Behavior |
| --- | --- |
| `HELLO` | Replies `ACK:HELLO` |
| `IDLE?` | Prints idle sense debug |
| `BTN:STBY` | Sends standby key datagram |
| `BTN:AUTO` | Sends auto key datagram |
| `BTN:TRACK` | Sends track key datagram |
| `BTN:WIND` | Sends wind key datagram |
| `BTN:M1` | Sends minus 1 key datagram |
| `BTN:M10` | Sends minus 10 key datagram |
| `BTN:P1` | Sends plus 1 key datagram |
| `BTN:P10` | Sends plus 10 key datagram |
| `BTN:TACK_PORT` | Sends tack port key datagram |
| `BTN:TACK_STBD` | Sends tack starboard key datagram |

SeaTalk transmit format:

The button commands transmit Thomas Knauf style `0x86` keystroke datagrams:

```txt
86 X1 YY yy
```

- `X1` is the source/type byte.
- The current source/type byte is `0x11`.
- `YY` is the key code.
- `yy` is the bitwise complement, implemented as `0xFF - keyCode`.
- The first byte is transmitted with the 9th command bit set.
- Remaining bytes are transmitted with the 9th command bit clear.

Current key mapping:

| Logical command | SeaTalk key code |
| --- | --- |
| `AUTO` | `0x01` |
| `STBY` | `0x02` |
| `TRACK` | `0x03` |
| `M1` | `0x05` |
| `M10` | `0x06` |
| `P1` | `0x07` |
| `P10` | `0x08` |
| `TACK_PORT` | `0x21` |
| `TACK_STBD` | `0x22` |
| `WIND` | `0x23` |

The `WIND` mapping is explicitly marked in the sketch as something to verify by sniffing a real control head before live use.

SeaTalk receive and decode:

The sketch prints every received datagram as `ST_RX:` followed by hex bytes. It currently decodes:

| Datagram | Meaning |
| --- | --- |
| `0x84` | Autopilot state, heading, setpoint, mode bits, and rudder angle |
| `0x10` | Apparent wind angle, printed as `WIND_ANGLE:<deg>` |
| `0x86` | Keypress datagram, printed as source/key/inverse |

Decoded autopilot status is printed in this format:

```txt
STATUS:MODE=<MODE>;RUDDER=<deg>;SETPOINT=<deg>
```

Modes are decoded from the low bits of the `0x84` datagram:

- Bit `0x08`: `TRACK`.
- Bit `0x04`: `WIND`.
- Bit `0x02`: `AUTO`.
- Otherwise: `STANDBY`.

## React dashboard

`marine-dashboard/` is a Vite React app named `marine-dashboard`. It is currently a static frontend project with mock-data fallback behavior.

Main files:

- `marine-dashboard/src/App.jsx`: application state, layout editor, data polling, widgets, and autopilot control UI.
- `marine-dashboard/src/App.css`: component styles and responsive dashboard grid.
- `marine-dashboard/src/index.css`: global theme variables and page defaults.
- `marine-dashboard/vite.config.js`: Vite config with `base: './'` so the app can be built as relative static assets.
- `marine-dashboard/package.json`: npm scripts and dependencies.

Run locally:

```sh
cd marine-dashboard
npm install
npm run dev
```

Build static files:

```sh
cd marine-dashboard
npm run build
```

Lint:

```sh
cd marine-dashboard
npm run lint
```

The dashboard depends on:

- React `^19.2.6`.
- React DOM `^19.2.6`.
- Vite `^8.0.12`.
- ESLint `^10.3.0` and React-related ESLint plugins.

### Dashboard data model

The dashboard polls:

```txt
GET /data
```

Polling behavior:

- Poll interval: `1000 ms`.
- Request timeout: `700 ms`.
- Request cache mode: `no-store`.
- If the endpoint returns a successful JSON response, the app merges it into the current data state.
- If the endpoint is unavailable, the app animates local mock data for desktop/browser development.

The current UI expects data shaped like this:

```json
{
  "heading": 184,
  "cog": 181,
  "sog": 5.4,
  "awa": 38,
  "depth": 12.4,
  "waterTemp": 18.6,
  "rudderAngle": -3,
  "battery0": 12.72,
  "battery1": 12.64,
  "autopilotMode": "AUTO",
  "targetHeading": 185,
  "seatalkStatus": "ok",
  "n2kStatus": "ok",
  "seatalkLastSeenMs": 150,
  "n2kLastSeenMs": 120,
  "wifiClients": 2,
  "uptime": 123456,
  "packetCounters": {
    "seatalkRaw": 814,
    "seatalkDecoded": 773,
    "n2kPgn": 432,
    "errors": 0
  },
  "rawSeatalk": ["84 20 B8 00"],
  "decodedSeatalk": ["Compass heading 184 deg"],
  "rawN2k": ["127250 heading"]
}
```

`batteryDiff` is computed in the browser from `battery0 - battery1`.

Data is considered stale for autopilot command safety when:

```txt
seatalkStatus != "ok" or seatalkLastSeenMs > 3000
```

The dashboard also displays NMEA 2000 staleness using `n2kLastSeenMs > 3000`.

### Dashboard command API

The autopilot panel posts commands to:

```txt
POST /api/autopilot
Content-Type: application/json
```

Command bodies currently emitted by the UI:

```json
{ "command": "standby" }
{ "command": "auto" }
{ "command": "wind" }
{ "command": "track" }
{ "command": "heading_delta", "value": -10 }
{ "command": "heading_delta", "value": -1 }
{ "command": "heading_delta", "value": 1 }
{ "command": "heading_delta", "value": 10 }
{ "command": "tack_port" }
{ "command": "tack_starboard" }
```

The UI is currently optimistic during browser development: failed posts are ignored, and the local mock state is updated for mode and heading-delta commands.

Current safety and usability behavior:

- Controls are locked by default.
- The unlock state is saved in browser `localStorage`.
- Commands are disabled when SeaTalk 1 status is stale.
- Mode and tack commands require a second tap within `2500 ms`.
- All commands share a `900 ms` cooldown after sending.
- The command log is session-local UI state only.

### Dashboard layout behavior

The dashboard starts with four pages:

- `Helm`.
- `Sailing`.
- `Electrical`.
- `Debug`.

Users can:

- Rename pages.
- Add pages.
- Delete pages, as long as at least one remains.
- Reset all pages to defaults.
- Show or hide widgets per page.
- Move widgets up and down in page order.
- Resize widgets with the lower-right drag handle.
- Resize widgets from the layout panel using width and height buttons.

Layout state is saved under:

```txt
m5-marine-dashboard-layout-v3
```

Autopilot control unlock state is saved under:

```txt
m5-marine-dashboard-controls-v1
```

Both are browser `localStorage` keys.

## Current integration status

This repo has several useful pieces, but it is not yet a single fully wired product.

What is wired in source today:

- The wind sender can join the M5Stamp PLC access point and send AS5600 angles over UDP.
- The M5Stamp PLC can receive those UDP packets, apply a persisted offset, show the value, and publish apparent wind angle to NMEA 2000.
- The M5Stamp PLC can sample two INA226 voltage monitors and publish battery voltage to NMEA 2000.
- The Arduino Due can listen to and transmit SeaTalk 1 datagrams through external interface hardware.
- The React dashboard can run locally, display mock marine data, persist layout changes, and issue HTTP autopilot command requests.

What is not wired in source today:

- `NEWM5Stam` does not serve the React dashboard static files.
- `NEWM5Stam` does not implement `GET /data`.
- `NEWM5Stam` does not implement `POST /api/autopilot`.
- `dueseatalk1` is not connected to the dashboard code.
- There is no bridge in this repo that maps dashboard `POST /api/autopilot` commands to Arduino Due USB serial commands like `BTN:AUTO`.
- There is no code in this repo that converts SeaTalk 1 status output from the Due into the dashboard's `seatalkStatus`, `rudderAngle`, `targetHeading`, or debug arrays.
- The dashboard mentions staging built files into `../ESP32DashboardTest/data/`, but that target project is not present in this repository.

A likely next integration step would be an HTTP bridge that:

1. Serves the dashboard build output.
2. Provides `GET /data` using current NMEA 2000, Wi-Fi, battery, and SeaTalk state.
3. Provides `POST /api/autopilot`.
4. Converts dashboard command names to the Due serial protocol:

```txt
standby        -> BTN:STBY
auto           -> BTN:AUTO
wind           -> BTN:WIND
track          -> BTN:TRACK
heading_delta -10 -> BTN:M10
heading_delta -1  -> BTN:M1
heading_delta 1   -> BTN:P1
heading_delta 10  -> BTN:P10
tack_port      -> BTN:TACK_PORT
tack_starboard -> BTN:TACK_STBD
```

## Build and upload notes

### Arduino sketches

Open each sketch folder in the Arduino IDE or build with Arduino CLI.

The sketches are independent Arduino projects:

- `NEWM5Stam/NEWM5Stam.ino`
- `WindWifiSenderESP32/WindWifiSenderESP32.ino`
- `dueseatalk1/dueseatalk1.ino`

Expected board targets based on code and existing build metadata:

| Sketch | Intended board |
| --- | --- |
| `NEWM5Stam` | M5Stack Stamp S3 / M5Stamp PLC, FQBN observed as `esp32:esp32:m5stack_stamp_s3` |
| `WindWifiSenderESP32` | Seeed Studio XIAO ESP32C3 or compatible ESP32C3 board |
| `dueseatalk1` | Arduino Due |

Arduino library dependencies visible from includes:

| Sketch | Libraries / headers |
| --- | --- |
| `NEWM5Stam` | `Arduino.h`, `Wire.h`, `WiFi.h`, `WiFiUdp.h`, `Preferences.h`, `M5Unified.hpp`, `M5StamPLC.h`, `NMEA2000.h`, `N2kMessages.h`, `NMEA2000_esp32_twai.h` |
| `WindWifiSenderESP32` | `Arduino.h`, `Wire.h`, `WiFi.h`, `WiFiUdp.h` |
| `dueseatalk1` | `Arduino.h` plus direct SAM3X/Due USART register access from the Arduino Due core |

Example Arduino CLI commands, adjust board names and ports for your machine:

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_stamp_s3 NEWM5Stam
arduino-cli upload -p COMx --fqbn esp32:esp32:m5stack_stamp_s3 NEWM5Stam
```

```sh
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 WindWifiSenderESP32
arduino-cli upload -p COMx --fqbn esp32:esp32:XIAO_ESP32C3 WindWifiSenderESP32
```

```sh
arduino-cli compile --fqbn arduino:sam:arduino_due_x dueseatalk1
arduino-cli upload -p COMx --fqbn arduino:sam:arduino_due_x dueseatalk1
```

The exact XIAO ESP32C3 FQBN depends on the installed ESP32 board package. Use:

```sh
arduino-cli board listall
```

to find the board identifier available in your environment.

### Dashboard

The dashboard can be developed and built independently:

```sh
cd marine-dashboard
npm install
npm run dev
npm run lint
npm run build
```

The Vite config uses:

```js
base: './'
```

That makes the built assets more suitable for being served from an embedded filesystem or a subdirectory, because references are relative instead of rooted at `/`.

## Protocol details

### Wind UDP protocol

Sender:

- Device: XIAO ESP32C3 running `WindWifiSenderESP32`.
- Destination: M5Stamp PLC AP IP `192.168.4.1`.
- UDP port: `20000`.
- Period: `100 ms`.

Payload:

```txt
angle=123.45
```

Receiver:

- Device: M5Stamp PLC running `NEWM5Stam`.
- Accepts either a string with `=` and a numeric value after it, or a plain numeric string.
- Wraps parsed angles into `[0, 360)`.
- Records sender IP, packet count, raw angle, and last packet time.
- Treats wind as stale after `3000 ms`.

### NMEA 2000 output

The M5Stamp PLC advertises transmit PGNs:

```txt
127508
130306
```

Battery status:

- Instance `0`: Port A INA226.
- Instance `1`: Port C INA226.
- Voltage is sent when available.
- Current and temperature are sent as `N2kDoubleNA`.

Wind data:

- Apparent wind angle is sent when fresh.
- Wind speed is sent as `N2kDoubleNA`.
- Wind reference is `N2kWind_Apparent`.

### SeaTalk 1 USB protocol

The Due sketch exposes a text protocol over USB serial. Lines are newline terminated. Carriage returns are ignored.

Recognized input:

```txt
HELLO
IDLE?
BTN:<COMMAND>
```

Common output:

```txt
ACK:HELLO
IDLE_DEBUG:SENSE_PIN_22=HIGH;BUS_IDLE=YES
BUTTON:AUTO
ST_TX: 86 11 01 FE
TX_OK
ST_RX: 84 ...
STATUS:MODE=AUTO;RUDDER=-3.0;SETPOINT=185
WIND_ANGLE:38.0
ERR:<reason>
```

Because `ENABLE_SEATALK_TX` is currently `1`, uploaded firmware will attempt real SeaTalk transmission when it receives button commands. Set it to `0` for bench testing without transmitting on the bus.

## Safety notes

This code can interact with real vessel electrical, NMEA 2000, Wi-Fi, and autopilot systems. Treat it as experimental until verified on your hardware.

Practical cautions:

- Do not connect the SeaTalk TX circuit to a live autopilot until the TX hardware, bus idle detection, RX polarity, echo checking, and key codes have been verified.
- The Due sketch has SeaTalk TX enabled by default.
- The `WIND` SeaTalk key code is marked for verification in the source.
- The dashboard UI has confirmation and stale-data lockouts, but those are browser-side safeguards only. Any future server or firmware endpoint should enforce its own command validation and lockouts.
- Confirm NMEA 2000 device identity, source address behavior, and PGN correctness on a test network before connecting to production equipment.
- Confirm power domains and level shifting for the SeaTalk interface. SeaTalk 1 electrical signaling is not the same as 3.3 V microcontroller UART signaling.

## Development notes

- Prefer editing the `.ino` files and dashboard source files, not generated Arduino build artifacts.
- If generated firmware binaries should not be versioned long term, add an ignore rule for `NEWM5Stam/build/` and remove the tracked build files in a separate cleanup commit.
- The dashboard already has mock data, so UI work can continue without hardware.
- The firmware and dashboard use different command/data protocols today. A bridge layer is needed before the browser UI can control the Due SeaTalk sketch or show live SeaTalk status.
- The current M5Stamp firmware is a NMEA 2000 node and Wi-Fi UDP receiver, not a web server.

## Quick verification checklist

For the current wind-to-NMEA path:

1. Flash `NEWM5Stam` to the M5Stamp PLC.
2. Open serial monitor at `115200`.
3. Confirm it prints the AP SSID, AP IP, UDP port, relay messages, and CAN status.
4. Flash `WindWifiSenderESP32` to the XIAO ESP32C3.
5. Open serial monitor at `115200`.
6. Confirm it connects to `M5StampPLC-Wind-CANOK` or `M5StampPLC-Wind-CANFAIL`.
7. Confirm the wind sender logs `Encoder angle ... deg` and `Sent angle=...`.
8. Confirm the M5 display shows `AWA` rather than `AWA waiting`.
9. Confirm an NMEA 2000 analyzer sees PGN `130306`.
10. Confirm an NMEA 2000 analyzer sees PGN `127508` for battery instances `0` and `1`.

For the SeaTalk controller:

1. Flash `dueseatalk1` to an Arduino Due.
2. Open serial monitor at `115200`.
3. Confirm boot output and idle polarity messages.
4. Send `HELLO` and confirm `ACK:HELLO`.
5. Send `IDLE?` and confirm the idle sense line is reasonable.
6. With TX disabled or on a safe test bus, send a `BTN:` command and confirm the expected `ST_TX:` line.
7. On a live receive-only test, confirm incoming datagrams print as `ST_RX:`.
8. Confirm decoded autopilot messages print `STATUS:MODE=...`.

For the dashboard:

1. Run `npm install` in `marine-dashboard`.
2. Run `npm run dev`.
3. Open the Vite dev URL.
4. Confirm mock data updates once per second.
5. Open `Layout`, add/remove/resize widgets, and refresh to confirm layout persistence.
6. Unlock controls and test command button UI in development mode. Failed HTTP posts are expected without a backend.

