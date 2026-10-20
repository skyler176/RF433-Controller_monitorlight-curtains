# Firmware layout

## Active development

- `rf433-bridge/`: CX9-B-A RF capture and replay on CoreS3.
- `sticks3-rf433-remote/`: active StickS3 firmware for the monitor light and
  curtains, with local buttons, Zenoh for the light, and HTTP for the curtains.

## Archived experiments

- `archive/sticks3-ir-mqtt-remote/`: superseded StickS3 IR experiment. It is
  preserved as a buildable reference for MQTT, NVS, display, and reconnect code.

Do not add CX9-B-A RF code to the archived IR project.
