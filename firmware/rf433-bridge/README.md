# CX9-B-A RF433 Bridge

Status: eight commands decoded and loaded for RF433T replay; lamp-side replay
verification is pending.

This is the active firmware development directory for the CX9-B-A monitor light.
The working physical-layer hypothesis is 433.92 MHz ASK/OOK, but implementation
must be driven by captured signals rather than assumed command formats.

## Target hardware

- CoreS3 development host.
- M5Stack Unit RF433R for pulse capture.
- M5Stack Unit RF433T for replay.
- Unit Hub connected to CoreS3 PORT.B, with RF433R and RF433T on the Hub.

## Current development wiring

| Grove wire | CoreS3 PORT.B | Unit signal |
| --- | --- | --- |
| Yellow | GPIO9 | RF433T `RF_TX` |
| White | GPIO8 | RF433R `RF_RX` |
| Red | 5V | Both units |
| Black | GND | Common ground |

The Hub directly parallels all four wires. This combination is valid because
RF433T uses only the yellow signal wire and RF433R uses only the white signal
wire. The probe explicitly enables the CoreS3 external 5V output.

## Probe build and use

```bash
pio run
```

The screen guides collection for eight actions: power, brightness CW/CCW, CCT
CW/CCW, and presets S1/S2/S3. Each action targets ten samples. A recording uses
a one-second hands-off baseline followed by a 2.5-second fixed signal window.
Filtered raw edge timings and baseline/signal statistics are stored as `.rfraw`
files in SPIFFS. `REPLAY` sends a verified command through RF433T; actions without
a verified code show `NO CODE`.

The current command set is power toggle, brightness CW/CCW, CCT CW/CCW, and
presets S1/S2/S3. The decoded command catalog is stored at
`../../rf-captures/cx9-ba-command-catalog.json`, and the complete 80 source
captures are under
`../../rf-captures/raw/2026-07-17-cores3-guided-8-actions/`. CCT CCW has nine
decodable samples; its first recording contains no complete frame.

To test a command, connect RF433T through the Hub, select the action with the
arrow buttons, and tap `REPLAY`. `RF SENT` confirms only that CoreS3 transmitted
the waveform; the lamp has no acknowledgement channel.

## Development gates

1. Confirm the carrier and receive stable pulses from every control action.
2. Save at least 10 raw samples per action under `../../rf-captures/raw/`.
3. Normalize and compare timings under `../../rf-captures/normalized/`.
4. Replay one command successfully at least 18 times out of 20.
5. Only then add MQTT and NovOS command mapping.

The replay implementation uses ESP32-S3 RMT hardware and timing normalized from
the repeated captures. Do not treat a successful transmit as confirmed lamp
state until the physical 20-trial replay gate passes.
