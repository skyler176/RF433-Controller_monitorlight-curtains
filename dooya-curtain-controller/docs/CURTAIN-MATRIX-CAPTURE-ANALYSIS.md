# Curtain Matrix Capture Analysis

Updated: 2026-07-23

## Dataset

The CoreS3 SPIFFS partition was exported read-only before cleanup:

```text
Archive: rf-captures/raw/2026-07-23-cores3-curtain-matrix/
Partition size: 1,572,864 bytes
SHA-256: 3e1a5eb287a8ed722a2f81f69bc5381e065d6d51bad961bfbb16a365d4f8eda9
Files: 92 total, including 48 curtain-matrix captures
```

## Usability

A strong movement sample contains structurally valid 71-bit frames. Up and down
normally contain two payloads repeated 5 and 3 times; stop normally contains one
payload repeated 6 times.

| Curtain | Up | Stop | Down |
| --- | ---: | ---: | ---: |
| 1 | 5 strong / 6 stored | 2 strong / 3 stored | 5 strong / 5 stored |
| 2 | 4 strong + 1 partial / 5 stored | 5 strong / 5 stored | 5 strong / 5 stored |
| 3 | 5 strong / 5 stored | 5 strong / 5 stored | 5 strong / 5 stored |
| 4 | 3 strong / 4 stored | 0 | 0 |
| 5 | 0 | 0 | 0 |

The partial curtain 2 up sample contains two valid repeated frames instead of
the normal 5+3 structure. It is useful as supporting evidence but should not
replace a strong sample.

Invalid captures:

- `curtain_01_up_04.rfraw`: receiver noise only, no valid movement frame.
- `curtain_01_stop_03.rfraw`: truncated at 12,288 bytes.
- `curtain_04_up_04.rfraw`: truncated at 12,288 bytes.

## Next recordings

To reach five strong samples for every curtain/action pair, record:

- curtain 1 stop: 3 more
- curtain 2 up: 1 more
- curtain 4 up: 2 more; stop: 5; down: 5
- curtain 5 up: 5; stop: 5; down: 5

This is 31 additional recordings. The invalid files remain archived as failure
evidence. Recording a new sample makes the newest replay candidate valid again.

## Storage resolution

SPIFFS was 1,363,432 / 1,438,481 bytes used, which caused the truncated files.
After the full partition backup, firmware startup removed the 44 archived legacy
flat-slot files. Usage fell to 670,672 / 1,438,481 bytes while all 48
`curtain_*` files remained on the CoreS3.

The firmware now reopens each saved capture and verifies its exact length before
reporting success. A close-time storage failure is reported as `verify_failed`
and its partial file is removed.

## Final compact migration

A second full partition export captured the later curtain 4 and 5 recordings:

```text
Archive: rf-captures/raw/2026-07-23-cores3-curtain-matrix-complete/
Partition size: 1,572,864 bytes
SHA-256: 341651fb11bd5be3e79ae1009bed122caef05d2489e0e2beb98ca391e3bd1d3c
```

Validated strong progress after this export:

| Curtain | Up | Stop | Down |
| --- | ---: | ---: | ---: |
| 1 | 5 | 2 | 5 |
| 2 | 4 | 5 | 5 |
| 3 | 5 | 5 | 5 |
| 4 | 5 | 5 | 5 |
| 5 | 5 | 3 | 0 |

The CoreS3 was migrated to a compact SPIFFS image containing one latest strong
waveform per populated slot plus a 21-byte progress file. Usage is now 194,274 /
1,438,481 bytes. Every new accepted recording overwrites the selected slot's
latest waveform instead of adding another raw file.

Recordings that remained immediately after migration:

- curtain 1 stop: 3
- curtain 2 up: 1
- curtain 5 stop: 2
- curtain 5 down: 5

## Completed dataset

All 15 slots now report five accepted captures. The final compact device state
was exported and verified:

```text
Archive: rf-captures/final-device-state/2026-07-23/
Partition size: 1,572,864 bytes
SHA-256: 18f1c5eae0c5da8813c91cd3a2b5bf68f295fb7f3904b436f433f2bed6bd2c43
Progress: all curtains 1-5 have up=5, stop=5, down=5
```

Each of the 15 retained latest files passed structural validation. See
`PROTOCOL-CLASSIFICATION.md` for the final rolling-code decision.
