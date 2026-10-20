# Dooya DC2702D Office Curtain Controller

This subproject adds five office curtains to the existing StickS3 remote without
adding the curtains to NovOS.

## Confirmed inputs

- Remote: Dooya `DC2702D`, 15-channel transmitter.
- Label frequency range: `433.05-434.79 MHz`.
- Office scope: use channel +/- to select curtain 1-5, then up, stop, or down.
- Development receiver: CoreS3 + M5Stack RF433R on PORT.B / GPIO8.
- Final transmitter: StickS3 + RF433T on GPIO9.
- Monitor-light control remains on zenoh-pico and NovOS.
- Curtain browser control is served directly by StickS3 on the local network.

## Current phase

Protocol capture and operational replay validation are complete. The final
catalog contains one waveform for each of the 15 curtain/action slots. All 15
passed immediate replay and a second complete replay pass after the original
remote was retired. The changing 71-bit protocol is not cryptographically
decoded; the selected product route is immutable replay under the explicit rule
that the original DC2702D will no longer be used.

The verified source image and generated StickS3 timing header are under
`rf-captures/final-verified-15-slot/2026-07-23/`.

Start here:

1. Read `docs/ARCHITECTURE.md`.
2. Read `docs/PROJECT-EXPERIENCE.md` for the decisions and deployment lessons.
3. Follow `docs/RF-LEARNING-PLAN.md`.
4. Build `firmware/cores3-rf-learner/`.
5. Store exported samples under `rf-captures/raw/<date>-cores3/`.

## Directory map

```text
dooya-curtain-controller/
  docs/                         decisions and RF learning procedure
  firmware/
    cores3-rf-learner/          capture-only firmware for the connected CoreS3
  rf-captures/
    raw/                        immutable exports from CoreS3
    normalized/                 derived frames and command catalogs
  tools/                        host-side capture inspection
```

The generated timing catalog and curtain-only HTTP frontend are now integrated
into `../firmware/sticks3-rf433-remote/`. Build, upload, HTTP, and the existing
Zenoh `/live` and `/info` paths have passed. The remaining gate is a physical
15-command curtain replay pass from StickS3 after power cycling.
