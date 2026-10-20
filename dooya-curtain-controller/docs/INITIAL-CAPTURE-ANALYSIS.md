# Initial DC2702D Capture Analysis

Updated: 2026-07-22

## Dataset

Read-only SPIFFS extraction from the connected CoreS3:

```text
channel_plus   10
channel_minus  10
up             10
stop           10
down            4
```

The 44 raw files and the complete SPIFFS partition image are archived under
`rf-captures/raw/2026-07-22-cores3-initial/`.

## Confirmed findings

### Channel selection is local state

Neither channel button produced a valid RF frame in any of its ten captures.
Their signal-window edge rates match baseline noise:

| Slot | Baseline edges/s | Signal edges/s | Change |
| --- | ---: | ---: | ---: |
| Channel + | 6528 | 6530 | 0.0% |
| Channel - | 6668 | 6721 | +0.8% |

The CoreS3 and final StickS3 UI must therefore own the selected curtain number.
Channel +/- are UI state changes, not RF transmissions.

### Movement commands are per-curtain rolling frames

Up, stop, and down contain a clear `350/700 us` PWM pulse family. Each frame
starts with approximately `H 4.78 ms + L 1.44 ms`, followed by 71 decoded data
bits. Consecutive frames are separated by approximately `7.7-8.7 ms` low gaps.

The leading 71-bit payload was unique in every independent sample:

| Action | Unique leaders | Samples | Pairwise Hamming distance |
| --- | ---: | ---: | --- |
| Up | 10 | 10 | min 27, mean 35.2, max 43 |
| Stop | 10 | 10 | min 25, mean 35.4, max 46 |
| Down | 4 | 4 | min 29, mean 35.3, max 39 |

This rules out a reusable static command catalog. The payload behavior is
consistent with rolling or encrypted commands.

### Press structure

- Up normally contains two different 71-bit payloads per capture, repeated
  approximately 5 and 3 times.
- Down shows the same 5 + 3 shape in the first three captures.
- Stop normally contains one payload repeated 6 times.
- `down_04.rfraw` contains additional valid payloads and likely includes more
  than one button actuation inside the capture window.

## Mapping of the current files

Based on the operator's notes:

- `up_01..10` are curtain 3 up.
- `down_01..04` are curtain 3 down.
- `stop_01..10` contain curtain 3 and curtain 4 stop samples, but the boundary
  is not encoded in the filenames.
- Channel +/- files are retained as negative evidence and should not be replayed
  as control commands.

Because the movement payload changes on every press, the stop samples cannot be
reliably assigned to curtain 3 or 4 from payload clustering alone. The sample
boundary must come from the recording order, or the commands must be recaptured
under curtain-specific filenames.

## Architecture consequence

The next learner UI should use two independent selections:

```text
Curtain: 1 2 3 4 5
Action:  UP STOP DOWN
Buttons: RECORD REPLAY
```

Files should be named `curtain_03_up_01.rfraw`, etc. Raw replay remains useful
as a diagnostic, but a used rolling frame may be rejected by the motor. A final
transmitter requires either a proven rolling-code implementation paired as a
new remote, or electrical actuation of the original remote.
