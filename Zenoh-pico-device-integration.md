# StickS3 Zenoh-Pico Device Integration

Updated: 2026-07-31

## Decision

Use zenoh-pico on StickS3 as the RF protocol leg, but keep the NovOS adapter as
an independent process.

The device owns only facts it can prove:

- which RF commands it supports;
- whether its RMT transmission completed;
- whether its Zenoh session is alive.

The later adapter owns NovOS concerns:

- the device descriptor and trait schema;
- mapping command-style CX9 controls to NovOS calls;
- NovOS request envelopes, structured errors, and presence policy;
- the distinction between command execution and desired/reported state.

This avoids exposing brightness or CCT as absolute state. The RF remote only
provides relative steps, a toggle, and three presets, and the lamp does not
report its resulting state.

## Device Contract

Prefix: `humanex/cx9-ba/<device-id>`

| Suffix | Primitive | Payload |
| --- | --- | --- |
| `command` | queryable | JSON command request and JSON result reply |
| `info` | queryable | JSON device metadata reply |
| `event` | put | JSON record for every local or remote transmit attempt |
| `live` | liveliness token | no payload |

Supported command IDs:

- `power_toggle`
- `brightness_cw`
- `brightness_ccw`
- `cct_cw`
- `cct_ccw`
- `preset_s1`
- `preset_s2`
- `preset_s3`

Request example:

```json
{"request_id":"tr-example","command":"cct_ccw","repeat":2}
```

Result example:

```json
{
  "protocol":"cx9-rf-zenoh/1",
  "device_id":"cx9-monitor-light",
  "request_id":"tr-example",
  "command":"cct_ccw",
  "repeat":2,
  "status":"sent",
  "hardware_confirmed":false
}
```

`sent` means only that the ESP32-S3 RMT peripheral completed the configured RF
waveform. It never means the lamp applied the command.

## Runtime Behavior

1. Local buttons and remote queries call the same RF transmit function.
2. A mutex prevents simultaneous local and remote RMT writes.
3. Wi-Fi and Zenoh run in a separate FreeRTOS task, so a blocked connection
   attempt cannot stall local buttons.
4. The local remote remains usable while Wi-Fi or Zenoh is offline.
5. Wi-Fi and Zenoh reconnect are retried every five seconds.
6. The Zenoh session runs in client mode and connects to an explicit router
   endpoint. Multicast discovery is not required.
7. Power and presets reject `repeat > 1`; relative brightness and CCT accept
   1-10 steps.
8. Every `/info` or `/command` query refreshes an inbound activity timestamp.
   If no query reaches the device for 30 seconds, firmware 0.3.3 treats the
   session as stale and forcibly reopens it. This covers half-dead sessions for
   which `z_session_is_closed()` has not changed yet.

## Device Health Endpoint

`GET /api/v1/health` is a local diagnostic surface. It reports:

- firmware version and uptime;
- Wi-Fi connected state, local IP, and RSSI;
- inferred Zenoh connected state;
- Zenoh session-open and inbound-timeout counters;
- age of the latest `/info` or `/command` query.

The endpoint does not report lamp power, brightness, or CCT. Those remain
unknown because the RF-controlled lamp has no feedback channel.

## 2026-07-31 Recovery Evidence

The StickS3 remained reachable over HTTP while its Zenoh TCP connection to the
router disappeared, proving that the failure was narrower than device power or
Wi-Fi reachability. Firmware 0.3.3 was built and flashed. A controlled test
paused the Mac-side CX9 adapter: after about 30 seconds the device watchdog
closed the stale session, reopened it, and the adapter observed the L2 link up
again at 17:48:00. No lamp command was sent during this recovery test.

## NovOS Adapter Status

The independent `novos-adapter-humanex-cx9` now provides the descriptor and
command-style trait, queries `<prefix>/info`, maps commands to
`<prefix>/command`, propagates the NovOS Action ID as `request_id`, and drives
L2 link presence from the device probe result. It deliberately publishes no
fictional absolute power, brightness, or CCT reported state.

Remaining work is operational validation: long-run availability, duplicate and
retry behavior, the complete Wi-Fi/router/device restart matrix, and the eight
commands' 18/20 lamp-side replay gate.

## Build And Deployment

The project pins zenoh-pico `1.9.0` and declares PlatformIO's CMake tool so a
system-wide CMake installation is not required.

```bash
cd firmware/sticks3-rf433-remote
cp include/network_config.example.h include/network_config.h
# Edit the local file with deployment values.
pio run
```

The local configuration file is ignored and must not be committed or included
in shared archives.
