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

# A whole update session, and one line cut in two

Two more files, added the same day for the same reason: a benchmark task built
on this protocol needs traffic that really happened.

## `update-session-capture.txt`

A complete session recorded with `capture-update-session.ps1`, after a stretch
of telemetry produced by pressing the button, so the update and the telemetry
share the wire in one file. What it shows, in order:

| Sent | Answered | Why it is in here |
| --- | --- | --- |
| `INFO_REQ` | `INFO` | the board says which version and bank it runs |
| `BEGIN` | `ACK` | a whole bank is erased before this answer |
| `DATA` at 0 | `ACK` | 256 image bytes go into flash |
| `DATA` at 256, checksum deliberately broken | `NAK` code `01`, BAD_CRC | **inside a session a corrupted frame IS answered**, unlike outside one, where the board stays silent |
| the same `DATA` at 256, clean | `ACK` | a `NAK` is never remembered, so a clean resend is judged afresh |
| `COMMIT` | `NAK` code `05`, FLASH_ERROR | the image verified in flash; switching banks is firmware piece 7, so the board refuses that step |

**A mistake worth keeping**, because it is exactly the kind a model makes: the
first version of this script sent the whole 256-byte image in one `DATA` and
then "resent" it at offset 0. The board answered `NAK BAD_OFFSET`, correctly,
because after a full image it waits at offset 256. A repeat only means
something at the offset the board is waiting for. The script was wrong, the
board was right, and the capture now sends two chunks so the resend lands
where it belongs.

## `mixed-wire-split-line.txt`

The same capture as `mixed-wire-capture.txt`, with **one telemetry burst cut
in two by hand**, between `equipment/0/` and the rest, with the second half
given a timestamp 40 ms later. The line marked `# CUT BY HAND here` says so in
the file itself.

Nothing on the board produced this split. A board writes a line in one
blocking call; a split is what a HOST sees when USB delivers a burst in two
pieces or a read returns early. It is in here because a reader that assumes a
telemetry line always arrives whole will pass every other test and fail on a
real wire.
