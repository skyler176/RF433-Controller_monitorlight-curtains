# DC2702D curtain controller project experience

## Product boundary

One StickS3 runs two logically separate systems over one serialized GPIO9
RF433 transmitter:

- The Humanex monitor light keeps its local controls and Zenoh-Pico/NovOS path.
- The five office curtains use local buttons and a StickS3-hosted HTTP page.
  Curtain commands never enter NovOS.

This separation is a product boundary, not a second firmware. Wi-Fi, the RMT
peripheral, and the transmission mutex are shared; contracts and events are
not.

## RF learning lessons

The DC2702D label describes a 15-channel 433 MHz remote, but the useful control
unit is not five generic buttons. Each curtain has its own up, stop, and down
rolling frame, producing 15 independent command slots.

Repeatedly using the original remote invalidated recordings that had worked
earlier. The viable replay route therefore required an explicit operational
rule: record one currently accepted waveform per slot, verify it immediately,
then retire the original remote. This is not equivalent to decoding or cloning
the rolling-code algorithm.

Raw captures must remain on the computer. The embedded device needs only the
final verified waveform for each slot. This removed the CoreS3 storage limit
without discarding forensic evidence. The final SPIFFS source image is identified
by SHA-256
`b10b0e298cbe5c1cba6559a2c4f108d605908b2547787e6405078d1ad82e1bea`.

## Hardware roles

- CoreS3 is the capture, replay-validation, and protocol-analysis tool.
- StickS3 is the deployed controller.
- RF433T uses GPIO9. RF433R is not required for normal operation.

Keeping capture tooling out of the deployed firmware reduced storage pressure
and prevented accidental overwriting of the accepted catalog.

## Responsive control

The validated waveform itself takes time: stop is about 545 ms, while up and
down are about 727 ms. Shortening these recordings would change the RF evidence
and risk acceptance failures.

The UI delay was solved at the scheduling boundary instead. HTTP requests enter
an eight-entry FIFO and return `202 queued` immediately. A dedicated task sends
the unchanged waveforms in arrival order. The page remains responsive, and an
up-then-stop sequence is preserved. `queued` means accepted by StickS3; it does
not mean the motor moved.

## Network addressing

The page and API are served directly by StickS3. No development computer,
NovOS process, or Zenoh router is required for curtain control once StickS3 is
powered and joined to Wi-Fi.

Use the StickS3 DHCP address as the reliable LAN entry point. The mDNS alias
`http://office-curtains.local/` works on some clients, but mobile compatibility
was inconsistent. It must remain an optional convenience rather than the only
published address.

Firmware-level static IP configuration was considered and rejected. A static
client does not inform the DHCP server that the address is occupied, can create
duplicate-IP failures, and becomes invalid when gateway, DNS, or subnet settings
change. The correct stable-IP mechanism is a router DHCP reservation for the
StickS3 MAC address.

The local HTTP service has no authentication or TLS and must not be exposed by
router port forwarding. Remote Internet control requires either an authenticated
VPN gateway or a separate cloud relay with outbound TLS from StickS3.

## Reliability semantics

This is a one-way RF system. The controller can prove only that a command was
queued and that RMT transmission completed locally. It cannot report curtain
position or motor execution. APIs, UI text, logs, and future integrations must
preserve that distinction.

The final acceptance pass should cover all 15 commands after a StickS3 power
cycle, an immediate up-then-stop sequence, multiple same-LAN clients, and Wi-Fi
reconnection. The original DC2702D remote must remain unused throughout.
