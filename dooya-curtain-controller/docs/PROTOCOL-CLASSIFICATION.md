# DC2702D Protocol Classification

Updated: 2026-07-23

## Current decision

The installed `DC2702D` uses a changing or encrypted 71-bit protocol. The
classic Dooya 40-bit encoder remains rejected, and the 71-bit semantic fields
and cryptographic behavior are not decoded.

The final controlled experiment retained exactly one newly recorded waveform
for each of the 15 curtain/action slots. Every slot worked immediately, and all
15 worked again in a complete pass after the operator stopped using the original
remote. This validates an immutable replay catalog for this installation under
one non-negotiable operational constraint: retire the original DC2702D remote.

Earlier mixed tests showed that later original-remote traffic can invalidate
captures. They do not establish the internal counter scope and are superseded as
the route decision by the final 15-slot verification.

## Evidence

The final CoreS3 progress record is complete:

```text
curtain 1  up=5 stop=5 down=5
curtain 2  up=5 stop=5 down=5
curtain 3  up=5 stop=5 down=5
curtain 4  up=5 stop=5 down=5
curtain 5  up=5 stop=5 down=5
```

All 15 retained latest waveforms are structurally complete:

- up and down: 8 valid frames, two unique payloads repeated 5+3 times;
- stop: 6 valid frames, one payload repeated 6 times;
- frame shape: approximately 7.7-8.2 ms gap, 4.7-4.8 ms sync high,
  1.4-1.5 ms delimiter, then 71 PWM bits.

Across the historical and final datasets:

```text
Structurally valid capture files analyzed: 81
Payload instances: 135
Different payloads: 116
Global fixed bit positions: 0 / 71
Bit-one rate range by position: 0.363-0.622
Typical same-slot pairwise Hamming distance: about 34 / 71 bits
```

The whole payload changes with avalanche-like behavior. There is no stable
sender/channel/button field available to copy into a fixed encoder. Up/down also
consume two changing payloads during one short press.

## Selected transmitter route

```text
Browser/local UI -> StickS3 -> verified immutable waveform -> curtain
```

The existing monitor-light path remains separate:

```text
NovOS -> Zenoh -> StickS3 -> GPIO9 RF433T -> Humanex monitor light
```

The curtain route does not enter NovOS. The 15 waveforms are compiled into the
StickS3 firmware from the verified source archive. No learning or replacement
operation is exposed in the production firmware.

## Operational constraint

Label and store the original remote as retired reference hardware. Do not use
its movement buttons after deployment. If any waveform later stops working,
return to CoreS3 capture and create a new versioned 15-slot archive; do not
silently replace individual production codes without a complete regression pass.
