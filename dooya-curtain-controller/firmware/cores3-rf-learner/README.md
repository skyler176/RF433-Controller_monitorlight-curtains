# CoreS3 DC2702D RF learner

Capture-only firmware for CoreS3 with RF433R on PORT.B:

```text
RF433R white wire -> GPIO8
RF433R red wire   -> 5V
RF433R black wire -> GND
```

The top row selects curtain 1-5 and the middle row selects up, stop, or down.
The firmware keeps one latest capture for each of these 15 independent slots.
It validates the expected 71-bit frame structure before accepting a capture and
stores progress separately. Each accepted capture overwrites a path such as
`latest_curtain_03_up.rfraw`, so storage use remains bounded. Record a slot,
replay it immediately, and use `RE-REC` to replace it if the motor does not
respond. The right-side `REPLAY` button extracts
and transmits only the complete validated frames from the latest capture through
an RF433T on GPIO9; receiver noise before and after those frames is not replayed.
Once a slot has a capture, the left button changes to `RE-REC` and overwrites
that slot's latest waveform without increasing storage use. A
successful send is not curtain acknowledgement, and a receiver may reject an
already-used changing-code frame.

All multi-sample raw datasets are archived on the development computer rather
than retained on the CoreS3.

On startup, the learner removes only the known CX9 capture filenames. Those
samples were already exported under the parent project's `rf-captures/` folder;
removing them frees SPIFFS space without touching DC2702D captures.

Build:

```bash
cd dooya-curtain-controller/firmware/cores3-rf-learner
pio run
```

Do not upload until existing CX9 SPIFFS data has been exported or confirmed
backed up. A firmware upload may preserve SPIFFS, but the project must not rely
on that behavior as its backup strategy.
