# DC2702D RF Learning Plan

Updated: 2026-07-23

## Goal

Determine whether the installed remote is using a stable Dooya frame or a
rolling-code mode, then derive a reproducible transmitter implementation for
curtains 1-5.

## Capture slots

The CoreS3 learner separates the five curtains and three movement commands into
15 slots:

- `curtain_01_up`, `curtain_01_stop`, `curtain_01_down`
- repeat the same three slots for curtains 2-5

First capture five strong independent presses per slot, then use the analysis to
decide which slots need ten. Use channel +/- on the original remote to select
the curtain shown on the CoreS3, then capture up, stop, or down. Do not record
channel +/-: current evidence shows that selection is local to the remote, while
each curtain has distinct movement frames.

## Procedure

1. Keep the CoreS3 and RF433R at a fixed distance from the remote.
2. Capture baseline noise before each press using the guided UI.
3. Use short, separate presses; do not hold the movement key during the first
   dataset.
4. Confirm that the curtain number on the original remote matches the CoreS3
   before every capture.
5. Export files without renaming or modifying them.
6. Repeat one curtain/action pair five more times after at least 20 unrelated
   remote presses.
7. Remove and reinstall the remote battery, then capture the same pair five
   times.
8. Record whether every original press still operates the curtain.
9. Only after analysis, test one candidate transmission against a motor.

## Classification checks

- Cluster pulse widths and synchronization gaps without assuming CX9 timings.
- Search for a repeated frame length; classic Dooya observations commonly use a
  40-bit shape with sender ID, channel, button, and check fields, but this is a
  hypothesis to test against this remote.
- Compare consecutive presses of the same channel/action bit for bit.
- Identify stable sender and channel fields, action-dependent fields, and any
  monotonic or pseudo-random field.
- Test whether a previously accepted raw frame is accepted again after the
  original remote sends a newer command.

## Acceptance gate

Protocol work may proceed to StickS3 only when:

- all 15 curtain/action slots have at least 5 strong captures;
- the frame and checksum/counter model explains every usable sample;
- up, stop, and down are verified on each real curtain;
- every final action succeeds at least 18 out of 20 attempts;
- behavior after original-remote use, battery replacement, and StickS3 reboot is
  documented.

`RF SENT` means transmitter completion only, never curtain acknowledgement.
