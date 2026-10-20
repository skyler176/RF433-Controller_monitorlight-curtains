# StickS3 IR MQTT Remote

> Historical experiment: this firmware still builds, but it is no longer the
> active CX9-B-A control path. PCB evidence indicates a likely 433.92 MHz
> ASK/OOK remote. Reuse the MQTT, NVS, display, and connection-management code;
> do not use the IR capture/transmit path for the next hardware validation.
> The project is intentionally preserved under `firmware/archive/`.

This firmware turns M5Stack StickS3 into a persistent IR learning remote with
optional MQTT control.

## Commands

- `power_on`
- `power_off`
- `brightness_up`
- `brightness_down`
- `warm`
- `neutral`
- `cool`
- `color_temp_up`
- `color_temp_down`

## Two-button operation

- Button A short press: select the next command.
- Button A hold for 0.9 seconds: learn the selected command.
- Button B short press: transmit the selected learned command.

During learning, point the original remote at StickS3 from more than 30 cm away
and press the corresponding remote button once. The raw RMT waveform is stored
in ESP32 NVS and remains available after restart.

The serial monitor prints every learned waveform between
`IR_CODE_JSON_BEGIN` and `IR_CODE_JSON_END` markers so it can also be backed up
outside the device.

## MQTT

Copy `include/config.example.h` to `include/config.local.h` and set the 2.4 GHz
Wi-Fi and broker values.

Publish a command with QoS 1 and retain disabled:

```json
{ "command": "power_on" }
```

Default topics:

```text
humanex-monitorlight/ir/cx9-ba/command
humanex-monitorlight/ir/cx9-ba/state
humanex-monitorlight/ir/cx9-ba/event
humanex-monitorlight/ir/cx9-ba/presence
```

`state` and `presence` are retained. `event` is not retained. A send event means
the StickS3 transmitted the learned waveform; `hardware_confirmed` remains false
because the monitor light has no IR return channel.

## Build, upload, monitor

On this Mac, the globally installed `pio` uses Python 3.9, while the selected
ESP32 platform requires Python 3.10 or newer. Use the existing `uvx` runner:

```bash
uvx --python 3.12 --from platformio platformio run
uvx --python 3.12 --from platformio platformio device monitor -b 115200
```

For USB upload, first long-press the StickS3 power button to enter download
mode. On this device, PlatformIO's automatic DTR/RTS reset disconnects the USB
port, so use esptool directly with `no-reset`:

```bash
python3 -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem11101 \
  --before no-reset \
  --after hard-reset \
  write-flash 0x0 .pio/build/m5stack-sticks3/firmware.factory.bin
```

The port name can change after reconnecting; check it with
`find /dev -maxdepth 1 -name 'cu.usbmodem*' -print` before uploading.
