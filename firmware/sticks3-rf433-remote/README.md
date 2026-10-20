# StickS3 Monitor Light and Curtain Remote

This is the active StickS3 deployment target. One firmware runs two independent
control paths over a shared RF433 transmitter:

- CX9-B-A monitor light: local buttons and the existing Zenoh/NovOS contract.
- Office curtains: local buttons and a StickS3-hosted HTTP page, outside NovOS.

CoreS3 remains the capture and protocol-analysis host.

## Wiring

Connect RF433T directly to the StickS3 HY2.0-4P `PORT.CUSTOM`, or connect it
through the passive Unit Hub when RF433R must remain attached.

| Grove wire | StickS3 | Unit signal |
| --- | --- | --- |
| Yellow | GPIO9 | RF433T `RF_TX` |
| White | GPIO10 | RF433R `RF_RX` when used |
| Red | 5V | Unit power |
| Black | GND | Common ground |

The firmware enables `EXT_5V_EN`. Do not feed external power into the Grove
5V wire while StickS3 is configured to output 5V.

## Two-button control

- Home: A enters monitor-light control; B enters curtain control.
- Monitor light: A selects the next action, B sends, and B hold repeats only
  brightness or CCT steps. A hold returns home.
- Curtain selection: A selects curtain 1-5, B enters its action list, and A hold
  returns home.
- Curtain action: A selects up, stop, or down; B sends once; A hold returns to
  curtain selection.

The action order is POWER, BRIGHT +, BRIGHT -, CCT CW, CCT CCW, and S1/S2/S3.
`BRIGHT +` maps to the recorded clockwise brightness code; this direction must
still be confirmed in the lamp-side replay trial.

## Curtain HTTP control

After Wi-Fi connects, open the StickS3 DHCP address from a client on the same
LAN. The HTML and API are both served by StickS3, so users do not need a
separate file or NovOS account.

`http://office-curtains.local/` remains a best-effort mDNS alias, but mobile
compatibility was not reliable enough to make it the primary address. The
firmware stays on DHCP. Reserve its address by MAC in your router if a stable
address is needed.

```text
GET  /api/v1/curtains
POST /api/v1/curtains/{1..5}/commands
```

The POST body is `{"command":"up|stop|down","request_id":"optional"}`.
Curtain HTTP commands are never published to Zenoh. HTTP 202 with `queued`
confirms that StickS3 accepted the command into its eight-entry FIFO; a separate
task sends the unchanged waveform in order. The curtain motors provide no
feedback.

## Build

```bash
pio run
```

Find the StickS3 serial port with `pio device list`; it can change after
reconnecting. Native USB upload may leave the board in ROM download mode. Exit
it without reflashing by replacing `<PORT>` with the actual port:

```bash
stty -f <PORT> -hupcl
python3 -m esptool \
  --chip esp32s3 --port <PORT> \
  --after watchdog-reset run
```

## Validation gates

1. Firmware 0.3.2 builds and uploads to the physical StickS3: verified.
2. Curtain page and read-only API respond on the local network: verified.
3. Invalid curtain commands return HTTP 400 without RF transmission: verified.
4. Existing Zenoh `/live` and `/info` remain available: verified.
5. mDNS advertises `office-curtains.local`, but mobile compatibility is not an
   acceptance dependency.
6. Page load is 4008 bytes and completed in about 196 ms on the test LAN:
   verified.
7. Verify queued up, stop, and down clicks preserve order on a real curtain.
8. Verify the new home/menu behavior with both physical buttons.
9. Verify all 15 curtain commands from StickS3 after power cycling.
10. Run the existing monitor-light 18/20 physical replay test before declaring
   RF replay fully verified.

## Wi-Fi and Zenoh

The firmware remains a working local remote when network configuration is
absent or the router is unavailable. Network work runs in a separate FreeRTOS
task and cannot block local button handling. To enable Zenoh:

1. Copy `include/network_config.example.h` to
   `include/network_config.h`.
2. Set the Wi-Fi SSID and password.
3. Set `CX9_ZENOH_CONNECT` to the NovOS router endpoint, for example
   `tcp/192.168.1.10:7447`.
4. Keep `CX9_DEVICE_ID` stable after the device is added to NovOS.

`network_config.h` is ignored and must not be committed.

Zenoh-pico is compiled in single-thread mode and advanced from the network task
with `zp_spin_once()`. This avoids the Arduino ESP32 background executor
starving the Wi-Fi/TCP path. Wi-Fi power saving is disabled for the persistent
router connection.

The StickS3 connects in Zenoh client mode and exposes this device-native
contract under `humanex/cx9-ba/<device-id>`:

| Key | Zenoh primitive | Purpose |
| --- | --- | --- |
| `/command` | queryable | Validate and transmit one RF command |
| `/info` | queryable | Return firmware, model, and supported commands |
| `/event` | publication | Report local or remote transmit attempts |
| `/live` | liveliness token | Report the firmware session as reachable |

The local HTTP endpoint `/api/v1/health` reports Wi-Fi state, the inferred
Zenoh state, session-open count, inbound-watchdog count, and the age of the
last `/info` or `/command` query. If no query reaches the device for 30 seconds,
the firmware treats the session as stale and reopens it instead of relying only
on `z_session_is_closed()`.

Command request:

```json
{"request_id":"req-1","command":"brightness_cw","repeat":3}
```

Command reply and event use `status: "sent"` only when RMT transmission
completed. They always include `hardware_confirmed: false`, because the lamp
has no feedback channel. `repeat` is limited to 1-10 and values greater than
one are accepted only for brightness and CCT commands.

This is intentionally a device-native contract. A separate NovOS adapter will
translate NovOS traits and drive requests into these queries; the firmware does
not embed NovOS descriptor or desired/reported policy.

## 2026-07-20 hardware verification

- Connected to the NovOS router at the locally configured endpoint.
- `/info` and `/live` returned from the physical StickS3.
- A `brightness_ccw` command returned `status: "sent"` and published a matching
  event with the same request ID.
- A 60-second short-timeout probe returned 28/30 replies; the two missed
  0.8-second windows recovered without intervention. Treat this as a Wi-Fi
  latency observation item, not lamp-side failure.
- `sent` confirms RMT completion only. This one-way RF device still has no
  lamp-side acknowledgement.
