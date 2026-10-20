# CX9-B-A RF captures

- `raw/`: unmodified GPIO or SDR captures plus action metadata.
- `normalized/`: timing-normalized samples used for comparison and replay.

Each action should have at least 10 independent captures. Filenames should keep
the action and sample number, for example:

```text
encoder_cw_step_01.json
s1_short_01.json
```

Do not place inferred command names in raw data before the lamp behavior has
been verified.

## 2026-07-17 CoreS3 dataset

- `raw/2026-07-17-cores3-guided-complete/`: 60 source captures, 10 each for
  power, brightness CW/CCW, and presets S1/S2/S3.
- `cx9-ba-command-catalog.json`: normalized 24-bit codes and replay timings.
- CCT CW/CCW are not part of this complete dataset and remain unverified.

## 2026-07-17 eight-action dataset

- `raw/2026-07-17-cores3-guided-8-actions/`: 80 source captures, 10 files for
  each of the eight screen actions.
- CCT CW decodes as `0x162AAF` in 10/10 samples.
- CCT CCW decodes as `0x162A5F` in 9/10 samples; `cct_ccw_01.rfraw` has no
  complete frame and remains preserved as an invalid raw attempt.
- See `../RF433-protocol-reproduction.md` for parameters and reproduction.
