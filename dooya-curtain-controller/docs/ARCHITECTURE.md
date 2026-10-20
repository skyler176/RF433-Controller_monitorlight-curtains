# Unified StickS3 architecture

Updated: 2026-07-23

## Product boundary

One StickS3 firmware hosts two logically independent device systems:

```text
Monitor light
  Local buttons ---------+
  NovOS -> Zenoh-Pico ---+-> CX9 commands -> shared RF433T

Office curtains
  Local buttons ---------+
  Browser -> HTTP/HTML --+-> verified curtain waveforms -> shared RF433T
```

The monitor-light path keeps its current Zenoh keys, payloads, liveliness, and
NovOS adapter behavior. Curtain commands are never registered in Zenoh and do
not enter NovOS. Both systems share Wi-Fi and the GPIO9 transmitter, but not
their command catalogs or network APIs.

This is one firmware image, not two independently flashed applications. A
single RF serialization point prevents simultaneous transmissions.

## Local button state machine

The StickS3 two-button home screen assigns one device to each button:

```text
Home:       A = MONITOR LIGHT    B = CURTAINS

Light:      A short = next command
            A hold  = home
            B click = send
            B hold  = preserve existing repeat behavior where supported

Curtain:    A short = next curtain
selection  A hold  = home
            B click = enter curtain

Curtain:    A short = next UP / STOP / DOWN
action     A hold  = curtain selection
            B click = send once
```

Curtain waveforms are never repeated by holding B. Each click sends exactly the
validated 6- or 8-frame capture.

## Curtain browser

StickS3 serves the actual curtain controller as `/`; there is no landing page.
The page shows curtains 1-5 with up, stop, and down controls. It calls only:

```text
GET  /api/v1/curtains
POST /api/v1/curtains/{1..5}/commands
     {"command":"up|stop|down","request_id":"..."}
```

The primary same-LAN address is the StickS3 DHCP address.
`http://office-curtains.local/` remains a best-effort
mDNS alias, but mobile resolution proved too inconsistent to make it the
primary path. Keep firmware on DHCP and reserve the IP by MAC in the router;
do not assign an uncoordinated static address in firmware.

The office requirement is that anyone who can reach the StickS3 on the local
network may control the curtains, so the first deployment has no login. Do not
port-forward or expose this HTTP service outside the office LAN. Validate the
curtain ID and action, return HTTP 202 when a command enters the eight-entry
FIFO, and return HTTP 503 when that queue is full. A dedicated task transmits
queued commands in arrival order so HTTP remains responsive during the 545-730
ms RF waveform.

An accepted HTTP response means only that the command was queued:

```json
{
  "status": "queued",
  "hardware_confirmed": false,
  "curtain": 3,
  "command": "down",
  "request_id": "req-123"
}
```

Do not report curtain position because the motor has no feedback channel.

## Firmware structure

Integrate incrementally into the existing active target:

```text
firmware/sticks3-rf433-remote/
  include/
    curtain_rf_codes.h          generated immutable 15-slot timing catalog
  src/
    main.cpp                    setup, buttons, and device-menu state
    rf_transmitter.*            sole owner of RMT channel and TX mutex
    cx9_system.*                existing fixed codes and Zenoh-facing actions
    curtain_system.*            waveform lookup and local command dispatch
    curtain_http.*              HTTP API and compiled HTML page
    cx9_zenoh.*                 existing Zenoh session, queryables, and events
```

The first refactor should move existing behavior without changing it, then add
the curtain modules. Do not fork the active firmware or duplicate Wi-Fi/RMT
initialization.

## RF data and memory

The source of truth is:

```text
dooya-curtain-controller/rf-captures/final-verified-15-slot/2026-07-23/
```

`export_sticks3_codes.py` removes receiver noise and generates 15 arrays of
alternating high/low durations. The catalog contains 16,060 edges, or 32,120
bytes of `uint16_t` timing data, and records the source SPIFFS SHA-256 in the
header.

Compile the generated arrays into application flash. At send time, convert one
waveform into the shared RMT buffer:

```text
maximum movement waveform: 1168 edges = 584 RMT symbols
maximum shared TX buffer:   584 symbols, approximately 2336 bytes RAM
```

This avoids a second filesystem image and prevents production codes from being
changed through the web interface.

## Runtime ownership

- Main loop: display and physical buttons.
- Existing Zenoh task: monitor-light network contract only.
- HTTP task: curtain page and API only.
- Shared RF transmitter: one RMT channel and mutex used by all three callers.
- Wi-Fi: initialized once; both network systems observe the same connection.

HTTP and local curtain sends must not publish CX9 Zenoh events. Zenoh commands
must not appear in the curtain HTTP event history. Serial logs use separate
prefixes such as `CX9_RF_REPLAY` and `CURTAIN_RF_REPLAY`.

## Deployment constraint

The original DC2702D remote is retired. The 15 production waveforms passed a
complete final replay test only under that condition. Production firmware does
not expose learning or replacement controls.

If any code later fails:

1. return to the CoreS3 learner;
2. create a new dated full SPIFFS archive;
3. validate all 15 slots again;
4. regenerate the header and source hash;
5. deploy a new firmware version.

Do not replace one embedded waveform ad hoc.

## Implementation gates

1. Existing CX9 local and Zenoh tests pass unchanged after module extraction.
2. Generated curtain header compiles and reports the expected source hash.
3. All 15 local StickS3 curtain commands pass real-motor replay.
4. Browser API passes all 15 commands and rejects invalid IDs/actions.
5. Concurrent local, HTTP, and Zenoh requests never overlap RF output.
6. Wi-Fi or Zenoh loss does not disable local control.
7. A final full 15-slot curtain pass succeeds after StickS3 power cycling.
