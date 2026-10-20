# Final verified 15-slot curtain catalog

This is the read-only CoreS3 SPIFFS export taken after the operator completed
immediate replay and a final replay pass for all 15 curtain/action slots.

Operational constraint: the original DC2702D remote is retired. Using it again
may invalidate one or more retained waveforms.

## Artifacts

```text
spiffs-partition.bin
  size: 1572864 bytes
  SHA-256: b10b0e298cbe5c1cba6559a2c4f108d605908b2547787e6405078d1ad82e1bea

generated/curtain_rf_codes.h
  SHA-256: b558213a954ec4e2a5c784a596cdff9c63ec74108e077e622d4d6b2d91ba3d89
  slots: 15
  clean edges: 16060 (32120 bytes of uint16_t timing data)
```

The extracted directory contains one `latest_curtain_XX_action.rfraw` file for
each curtain 1-5 and action up/stop/down, plus the 21-byte progress record.

Strict validation passed for every slot:

- up/down: 8 frames with payload repeats 5+3;
- stop: 6 frames with one payload repeated 6 times;
- no capture overflow;
- every frame has the expected sync, delimiter, 71 PWM bits, terminal pulse,
  and inter-frame gap.

Regenerate the StickS3 header with:

```bash
python3 dooya-curtain-controller/tools/export_sticks3_codes.py \
  dooya-curtain-controller/rf-captures/final-verified-15-slot/2026-07-23/extracted \
  dooya-curtain-controller/rf-captures/final-verified-15-slot/2026-07-23/generated/curtain_rf_codes.h \
  --source-image dooya-curtain-controller/rf-captures/final-verified-15-slot/2026-07-23/spiffs-partition.bin
```

Do not replace this archive after future experiments. Create a new dated export.
