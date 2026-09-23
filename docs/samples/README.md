# A real capture of the mixed wire

`mixed-wire-capture.txt` is what the serial line between this board and a PC
actually carries: telemetry text and binary flash-protocol frames, on one
wire, with no mode switch between them. Recorded on 2026-09-23 with
`capture-mixed-wire.ps1`, which logs every byte with the time it arrived and
sends three frames while it records.

The protocol is industrial-hmi `docs/protocols/uart-flash-v1.md`.

## How to read a line

```
14:13:47.050  RX  A5 81 01 00 06 00 01 00 00 00 01 00 F3 1C  |..............|
```

Time the bytes reached the PC, direction (`RX` from the board, `TX` to it),
the bytes in hex, then the same bytes rendered as text, with `.` for anything
that is not printable.

## What the capture contains, and why each part is there

- **Telemetry text**, `equipment/0/state,on\ntemp,24.4\n`, one burst per
  button press. This is the traffic the flash protocol has to coexist with.
- **14:13:46.992 TX**, an `INFO_REQ`, and **14:13:47.050 RX**, the board's
  `INFO` answer, **arriving between two telemetry bursts**. A reader that
  assumes the wire carries either text or frames, but not both interleaved,
  breaks here.
- **14:13:53.005 TX**, a frame whose checksum is deliberately wrong
  (`A5 01 02 00 00 00 00 00`). The board answers **nothing**, which is the
  rule for a corrupted frame outside a session: it is almost always line
  noise, and a NAK would put binary bytes into the telemetry stream for
  nothing.
- **14:13:59.032 TX**, a good `INFO_REQ` right after the bad frame, and the
  board answers it normally. This is the resync rule working: a false start
  or a bad checksum must not swallow the frame that follows it.

## What it does NOT contain

A telemetry line split across two bursts. Every line here arrived whole,
because the board writes a line in one blocking call and USB delivered it in
one piece. A split is a host-side reality (USB buffering, a read that returns
early), not something the board produces on demand, so cutting a burst in two
is a fair way to build that case from this capture.

## The bytes are the contract

`A5` starts a frame and is not a printable character, so it can never be
mistaken for the start of a telemetry line. The checksum is CRC-16/CCITT-FALSE
over TYPE..PAYLOAD, never over the `A5`, little-endian on the wire. The worked
example in the spec is `A5 01 01 00 00 00 E9 CD`, the first frame sent here.
