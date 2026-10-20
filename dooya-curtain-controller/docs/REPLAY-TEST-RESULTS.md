# DC2702D Replay Test Results

Updated: 2026-07-23

## First physical pass

The first real-motor test produced mixed results. This matrix records the
operator report; unlisted nuance such as distance and repeat count is not yet
controlled.

| Curtain | Up | Stop | Down |
| --- | --- | --- | --- |
| 1 | Failed | Worked | Worked |
| 2 | Worked | Worked | Worked |
| 3 | Worked | Worked | Worked |
| 4 | Failed | Failed | Failed |
| 5 | Failed | Failed | Failed |

Interpretation: a changing 71-bit payload does not by itself prove that every
motor rejects replay. At least eight retained waveforms physically controlled
their curtains. The project must distinguish reusable replay, acceptance-window
behavior, sample-specific failure, and RF range before selecting the final
transmitter route.

## Successful versus failed captures

All 15 retained files have the same valid frame structure. Movement commands
contain eight frames with two payloads repeated 5+3 times; stop commands contain
six repeats of one payload. No retained file is truncated or marked as overflow.

| Metric (mean) | Worked slots | Failed slots |
| --- | ---: | ---: |
| Stored edges in 2.5 s | 4491 | 4455 |
| Sync high | 4774 us | 4771 us |
| Delimiter low | 1449 us | 1450 us |
| PWM short pulse | 358 us | 358 us |
| PWM long pulse | 715 us | 715 us |
| Inter-frame gap | 8126 us | 7942 us |

The gap difference is not diagnostic: working samples span 7873-8468 us and
overlap the failed group. Only 19-27% of each stored capture belongs to validated
frames; the rest is receiver activity around the remote transmission. The first
firmware replay sent the entire capture window, so clean-frame replay must be
tested before replacing otherwise valid recordings.

Firmware now extracts only complete frames during replay. Full slots expose a
`RE-REC` button that overwrites the latest waveform while leaving the count at
five and preserving the archived original on the computer.

## Second physical pass: curtain 4 state invalidation

Curtain 4 up worked immediately after it was re-recorded. After stop and down
were also re-recorded with the original remote, only the newest down capture
continued to work. A read-only SPIFFS export confirmed that exactly the three
curtain 4 files changed; every other slot remained byte-identical to the final
archive.

All three new curtain 4 files are structurally strong:

| Action | Frames | Payload repeats | New timing (gap / sync / delimiter) |
| --- | ---: | ---: | --- |
| Up | 8 | 5+3 | 7874 / 4764 / 1450 us |
| Stop | 6 | 6 | 7786 / 4765 / 1453 us |
| Down | 8 | 5+3 | 8053 / 4766 / 1449 us |

Each new payload differs from its previous version by 29-38 of 71 bits. The
failure therefore follows command age, not malformed capture timing. The best
current model is a transmitter-wide or paired-channel rolling state: later
original-remote commands advance receiver state and invalidate older recorded
commands, even when the older command is a different action.

Post-test read-only archive:

```text
rf-captures/raw/2026-07-23-post-rerecord/
SHA-256: 77883270e52c6300c654aaa0fecba540a853427aa7079d7829ec11dc551d6dbb
```

## Required controlled tests

### Reuse test

Use curtain 2 up as the known-success sample:

1. Put curtain 2 near the middle with the original remote.
2. Replay `CURTAIN 2 / UP` three times, returning it near the middle before each
   attempt if needed.
3. Send any curtain 2 command with the original remote, then replay the old
   `CURTAIN 2 / UP` again.
4. Record success/failure for all four replay attempts.

If the same payload continues working after original-remote traffic, it is
operationally reusable even though its bits differ between original presses.

### Range test

Repeat one curtain 4 and one curtain 5 replay with the CoreS3/RF433T placed near
the target motor. Keep power, antenna orientation, and command unchanged. This
separates protocol rejection from link-budget failure.

### Alternate-sample test

First replay the current failed slots with clean-frame firmware. If a slot still
fails, use `RE-REC` once and replay it immediately. Start with curtain 1 up, then
curtain 4 up/stop/down and curtain 5 up/stop/down. Do not infer a per-curtain
protocol difference from one retained latest sample.

## Gate

Do not merge curtain control into StickS3 until:

- one successful slot passes repeated replay after original-remote use;
- curtains 4 and 5 have close-range and alternate-sample results;
- every final slot has a documented direct-replay or button-bridge route.

The curtain 4 result rejects a fixed raw-waveform catalog for unattended
control. Direct StickS3 RF remains viable only if the transmitter algorithm and
pairing state can be implemented as a newly paired remote; otherwise use a
button bridge to the original remote.

## Cross-channel counter test

Later original-remote up/down operations on curtain 3 invalidated previously
working curtain 3 recordings, while curtain 4 down remained usable. Curtain 2
and curtain 3 recordings can be stale while curtain 4 retains a working newer
recording. This rules out one global freshness counter shared by all 15 remote
channels. The receiver state is at least independent per curtain/channel, and
commands within one channel share that state.

The direct-RF implementation should therefore pair StickS3 as a separate new
transmitter identity and persist five independent channel states. Cloning the
existing remote identity would make manual remote use and StickS3 advance
competing counters.

## Fifteen-slot independence experiment

The learner now keeps one latest capture for each curtain/action slot. For each
slot, record once and replay immediately. If the motor does not respond,
overwrite that slot with `RE-REC` and retry until its latest capture works.

Test order:

1. Complete curtain 1 up, stop, and down, then recheck all three without using
   the original remote for curtain 1.
2. Repeat for curtains 2 through 5.
3. After all 15 slots pass their immediate test, do a second complete replay
   pass without touching the original remote at all.

The hypothesis passes only if all 15 slots still work in the final pass. An
immediate successful replay is insufficient because the receiver may tolerate
duplicates from the current button burst. If recording a later action makes an
earlier action on the same curtain fail, the three actions do not have
independent rolling state and the fixed-capture route remains rejected.

## Final result

The operator confirmed that all 15 newly retained slots passed immediate replay
and the complete final replay pass. The original remote is now retired and will
not be used for normal operation. The fixed catalog is accepted for the office
deployment under that constraint.

The verified CoreS3 SPIFFS image was exported read-only after this test:

```text
rf-captures/final-verified-15-slot/2026-07-23/spiffs-partition.bin
SHA-256: b10b0e298cbe5c1cba6559a2c4f108d605908b2547787e6405078d1ad82e1bea
Validation: 15/15 slots passed strict frame extraction
```
