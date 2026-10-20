# DC2702D RF captures

- `raw/`: immutable `.rfraw` exports from CoreS3, grouped by capture date.
- `normalized/`: derived timing clusters, decoded frames, and command catalogs.

The learner uses capture magic `0x44593252` (`DY2R`) and format version 1. Raw
files must not be edited after export. Record invalid/noisy attempts rather than
silently replacing them.

## 2026-07-22 initial dataset

`raw/2026-07-22-cores3-initial/` contains 44 extracted captures plus the complete
read-only SPIFFS partition image. See `../docs/INITIAL-CAPTURE-ANALYSIS.md` for
the fixed-versus-rolling classification and the known curtain mapping.

## 2026-07-23 curtain matrix dataset

`raw/2026-07-23-cores3-curtain-matrix/` contains the complete pre-cleanup SPIFFS
partition and its 92 extracted files, including 48 curtain-specific captures.
See `../docs/CURTAIN-MATRIX-CAPTURE-ANALYSIS.md` for per-slot usability and the
remaining recording list.

`raw/2026-07-23-cores3-curtain-matrix-complete/` is the final full raw export
before the CoreS3 moved to bounded latest-sample storage. `compact-device-image/`
contains the verified per-slot latest waveforms and progress data installed on
the device.

`final-device-state/2026-07-23/` contains the completed compact dataset: all 15
slots at five accepted captures and one validated latest waveform per slot.
