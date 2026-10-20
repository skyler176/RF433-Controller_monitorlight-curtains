# M5Stack RF433: monitor light and curtain control

Two related 433 MHz RF projects share one M5Stack StickS3 transmitter firmware.
The CoreS3 is the capture and analysis tool; the StickS3 is the remote. The
devices and control paths remain distinct:

| Project | Target | Control path | Start here |
| --- | --- | --- | --- |
| Humanex CX9-B-A monitor light | 8 captured commands | StickS3 buttons or Zenoh/NovOS | [RF protocol](RF433-protocol-reproduction.md), [capture data](rf-captures/README.md) |
| Dooya DC2702D office curtains | 5 curtains x up/stop/down | StickS3 buttons or local HTTP page | [Curtain project](dooya-curtain-controller/README.md) |

Both use M5Stack RF433R/RF433T units. The monitor light uses a decoded 24-bit
static code. The curtain project replays 15 validated waveforms from a changing
71-bit protocol; its original remote was retired during validation. These are
captures from specific devices, not universal codes for every CX9-B-A or DC2702D.

## Repository map

```text
firmware/rf433-bridge/                 CoreS3 monitor-light capture and replay
firmware/sticks3-rf433-remote/         active StickS3 firmware for both projects
dooya-curtain-controller/             curtain learner, analysis and captures
rf-captures/                           monitor-light captures and command catalog
tools/                                 host-side monitor-light analysis scripts
firmware/archive/                      superseded IR/MQTT experiment
```

The root-level research and status notes document the monitor-light work.
Earlier IR plans are historical; the RF433 firmware above is the current path.
The curtain project has its own [architecture and test notes](dooya-curtain-controller/docs/ARCHITECTURE.md).

## Hardware and build

- CoreS3 + RF433R on PORT.B GPIO8 for capture; add RF433T on GPIO9 for replay.
- StickS3 + RF433T on PORT.CUSTOM GPIO9 for local control. RF433R on GPIO10 is
  optional during development. See the [wiring details](firmware/sticks3-rf433-remote/README.md).
- Install Python 3 and [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

From the repository root, build the three active targets separately:

```bash
pio run -d firmware/rf433-bridge
pio run -d dooya-curtain-controller/firmware/cores3-rf-learner
pio run -d firmware/sticks3-rf433-remote
```

For network control, copy
`firmware/sticks3-rf433-remote/include/network_config.example.h` to
`network_config.h` in the same directory and set your own Wi-Fi and Zenoh
values. The local config is ignored by Git. Without it, the StickS3 buttons
still work, while the HTTP page and Zenoh connection require Wi-Fi.

The [StickS3 README](firmware/sticks3-rf433-remote/README.md) covers button
operation, the HTTP API, and the Zenoh contract. Use `pio run -d <target> -t
upload` only after selecting the intended board and checking its serial port.
Back up CoreS3 captures before uploading a different learner firmware.

## Verification and limits

The monitor-light eight-command catalog and the curtain 15-slot dataset were
captured and replayed on the development hardware. The remaining gates in the
[monitor-light status](PROJECT-STATUS.md) and [StickS3 README](firmware/sticks3-rf433-remote/README.md)
include full physical reliability trials and a post-power-cycle test of all
curtain commands from StickS3. A `sent` or `queued` response means the device
accepted or transmitted a command; neither target returns physical state.

The curtain HTTP service has no authentication or TLS. Keep it on a trusted
local network without port forwarding. RF captures and generated waveforms can
operate the matching equipment; use them only with equipment you are authorized
to control. Replace device-specific captures when adapting this code to a new
installation.
