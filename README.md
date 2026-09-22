# hmi-edge-node

[![CI](https://github.com/bogdanbaloi/hmi-edge-node/actions/workflows/ci.yml/badge.svg)](https://github.com/bogdanbaloi/hmi-edge-node/actions/workflows/ci.yml)

Bare-metal STM32 firmware for a **Nucleo-L476RG** (ARM Cortex-M4). It is the
device side of a hardware/software integration demo: it reads the physical
world (a button, the MCU's internal temperature sensor) and streams telemetry
over a serial link to the [industrial-hmi](https://github.com/bogdanbaloi/industrial-hmi) host, which
toggles a production line in its dashboard.

Written **register-level, no HAL and no CMSIS**: every peripheral is brought
up by hand from the reference manual (RM0351). The register map lives in
`Inc/registers.h`.

## What it does

On each press of the USER button (B1) it sends two lines over USART2:

```
equipment/0/state,on\n      (toggles on each press)
temp,23.5\n                 (die temperature in Celsius, one decimal)
```

The on-board LED (LD2) mirrors the equipment state. Warm the die and the `temp`
value rises.

### Getting degrees, not counts

The ADC gives raw counts. Turning them into a temperature takes the three
constants ST burns into system memory at the factory: the sensor's counts at
30 °C and at 130 °C, plus the internal reference VREFINT.

The catch is that all three were measured at **VDDA = 3.0 V** and a Nucleo runs
at **3.3 V**. The same die then produces about 10% fewer counts than the
calibration line expects, so applying it directly reads a 30 °C die as −0.7 °C:
wrong by thirty degrees, and plausible enough that nobody notices. Reading
VREFINT (a bandgap reference, steady against VDDA) recovers the scale factor.

The arithmetic is **integer only**, and the result is in tenths of a degree,
which is exactly what the wire format wants. The whole conversion costs 162
bytes of Thumb code and pulls in no runtime helpers.

That choice predates the FPU being switched on (see below) and survives it: a
tenth of a degree is finer than the sensor's accuracy, so a float would buy
precision the hardware does not have, at the cost of size and of rounding that
has to be undone before printing.

When no honest reading exists (blank calibration, a dead reference, a result
outside the sensor's range) the firmware **omits the temperature frame** rather
than sending a sentinel. A number on the wire that is not a reading is worse
than no reading, because a dashboard cannot tell them apart.

## The link to industrial-hmi (a protocol, not code)

The two repos are independent. They interoperate through a **serial wire
protocol** (`sensorId,value\n` lines at 115200 8N1), the same way a browser
and a web server interoperate through HTTP. This firmware is the reference
device; the host's `SerialBackend` is device-agnostic and parses the same
frames. The canonical protocol is defined in industrial-hmi (ADR-0029).

```
button / sensor -> this firmware (USART2) -> USB / ST-Link -> COMx -> industrial-hmi SerialBackend
```

## Architecture (HAL / app layering)

The application logic never touches a register, so it is portable and testable
on a host PC:

| Layer | Files | Job |
| --- | --- | --- |
| HAL / BSP | `core`, `board`, `uart`, `adc`, `registers.h` | the only code that touches registers |
| App | `telemetry`, `temperature` | pure logic: a button edge -> the frames, and counts -> degrees |
| Composition | `main.c` | wires the HAL to the app and runs the loop |

`telemetry` depends on injected function pointers (a temperature reader and a
line sink), so a host test drives it with a fake reader and a capturing sink.
No board needed to test the frame logic.

`adc` reports counts and the factory constants but deliberately does **not**
convert to degrees: that is arithmetic, and it lives in `temperature` where a
host test can reach it. The adapter that joins the two sits in `main.c`, the
only place a driver and pure logic are supposed to meet.

That adapter is also where the two readings get their types. The conversion
needs counts from the temperature channel and counts from VREFINT, both
`uint32_t`, both in the same range. Passed as bare integers they sat side by
side in the signature, and swapping them compiled cleanly and returned a
temperature that looked entirely reasonable. Nothing caught it: not the
compiler, not clang-tidy, and not the tests, because the mistake would live in
`main.c`, which no host test reaches.

They now have distinct types, `ts_counts_t` and `vrefint_counts_t`, so a swap
is a compile error instead of a plausible wrong number. The wrapping happens in
`main.c` rather than in `adc`, which would read better but would make the HAL
depend on application code and invert the layering. **The whole thing costs
zero bytes**: `temperature.o` is 162 bytes before and after, because the
single-field structs compile away entirely.

`core` is separate from `board` on purpose. `board` owns what changes when you
swap the board; `core` owns the CPU itself, which is the same on every
Cortex-M4 and is described by the ARM architecture manual rather than by
RM0351. It brings up the FPU and the millisecond clock.

### Debouncing is a decision, not a pause

A button is two pieces of metal meeting, and they bounce apart several times
over the first few milliseconds. The pin shows that as a burst of edges, and a
processor polling at megahertz sees every one of them.

This used to be `busy_wait(120000)` in the main loop: count NOP instructions
until roughly 30 ms had passed. It worked, and it had three problems. It froze
the processor for 30 ms. The constant meant 30 ms **only at 4 MHz**, so raising
the clock would have quietly shortened it below the bounce. And it lived in
`main.c`, which no host test can reach.

Now `telemetry` asks a question instead: has it been at least
`TELEMETRY_DEBOUNCE_MS` since the last accepted edge? Nothing stalls, the time
is real milliseconds from SysTick, and because it is pure logic a test can
drive it with any clock, including one about to wrap.

That last case matters. The comparison is written as a **subtraction**,
`now - last >= DEBOUNCE`, not as `now >= last + DEBOUNCE`. The clock wraps every
49 days, and the second form overflows near the top, so the button would stop
responding until the counter came round. There is a test that walks the clock
through `UINT32_MAX` to prove it does not.

`docs/uml/debounce.puml` has the whole picture, including why SysTick runs free
rather than reloading every millisecond.

### The FPU has two switches

Worth spelling out, because it is a trap that nothing catches for you.

| Switch | Where | What it does |
| --- | --- | --- |
| Build time | `-mfloat-abi=hard -mfpu=fpv4-sp-d16` | tells the **compiler** an FPU exists, so float code becomes VFP instructions |
| Run time | `CPACR` at `0xE000ED88` | grants the **core** access to it. Resets to denied, so the FPU is off after every reset |

This project had the first and not the second. Nothing complained, because
nothing used a float. But any float added later would have compiled silently
into a VFP instruction, hit a disabled unit, raised a UsageFault with the NOCP
bit, escalated to HardFault, and landed in the startup file's default handler,
which is an infinite loop. The board would just freeze, with nothing on the
serial line to explain it.

Nothing catches that mismatch: not the compiler (it was told an FPU exists),
not the linker, and not the host tests or CI, because a PC has a working FPU.
`core_enable_fpu()` runs from `SystemInit`, before `.data` is copied and before
`main`, which is the earliest point any C code could execute a float.


### A fault that says something

The FPU story above ends with the board freezing in the startup file's default
handler. That handler is two instructions:

```
Default_Handler:
Infinite_Loop:
  b Infinite_Loop
```

Every fault vector points there. So a hard fault looks exactly like a working
board that happens to be idle: LED unchanged, serial silent, nothing to see.
You find out only because the button stopped doing anything, and then you have
no idea why.

That is the same class of problem as the rest of this file, and it got the same
treatment: make the failure say something.

| Blinks | Fault |
| --- | --- |
| 2 | HardFault |
| 3 | MemManage |
| 4 | BusFault |
| 5 | UsageFault |

The LED repeats the count forever, with a longer gap between repetitions. You
count it from across the room and you know what happened, with no debugger
attached.

Three details that are not arbitrary:

**Counts start at two, never one.** A single blink is hard to tell apart from a
board flickering as it resets.

**The handler calls `board_init` again before blinking.** If the fault lands
before `main` got that far, the GPIO clock is still gated off and the LED stays
dark, which is the exact silence the whole thing exists to remove. Repeating the
setup costs a few register writes.

**The delays are nop loops, not `core_millis`.** A fault handler must not depend
on SysTick still running, on interrupts, or on any state the fault may have
corrupted. The timing is rough and that is fine: the eye only has to tell a
blink from a gap.

The startup file is not edited. It declares each handler `.weak` and aliases it
to `Default_Handler`, so defining the same four symbols in `Src/fault.c`
replaces those aliases at link time, and the vendor-generated file stays
untouched. Verified by dumping the linked vector table: offset `0x0C` holds
`0x080004c1`, which is `HardFault_Handler`, not `Default_Handler`.

`docs/uml/fault-signal.puml` has the before and after.
## Tests

```
mingw32-make -C tests run
```

No board, no test framework, no dependency on the other repo. It compiles the
pure-logic sources with the desktop compiler already on PATH. Five binaries,
five different questions:

### `contract_frame_test`: does the wire format still match?

The wire protocol is the one thing two independent repos have to agree on, and
nothing in a compiler checks it. This closes that gap: the build goes red if
this firmware stops emitting what the host can parse.

It is built in two layers, and that shape is the point:

| Layer | What it pins | Fails when |
| --- | --- | --- |
| `frame_gate` | the host parser's acceptance rules, driven by samples copied verbatim from its `SerialFrameParserTest` | the transcribed rules drift from the host's |
| `contract_frame_test` | telemetry's real output, pushed through that gate and asserted byte for byte | this firmware changes the frame format |

With literal assertions alone, a format change could be "fixed" on both sides at
once and the contract would break in silence. The gate is anchored to the host's
own samples, so it cannot be edited into agreement.

Covered: the two frames per press and their order, the on/off toggle, the press
edge (a held button emits nothing), the decimal formatting including negatives,
and the case where no temperature is available.

### `temperature_test`: is the number right?

A separate question, so a separate binary: a red build should say which one
broke. Covered: both calibration points reproduce exactly, the VDDA correction
recovers a 30 °C die from a 3.3 V reading, every guard against degenerate input,
and a sweep of the whole input domain checking monotonicity, which is what
catches wrapped arithmetic.

One test asserts the *size of the error* you get by skipping the VREFINT
correction, so that nobody simplifies it away without the build objecting.


### `fault_test`: can a person actually tell the patterns apart?

Most of a fault handler is not testable on a host. Whether the LED physically
lights is a hardware question, and whether the vector table points at the new
handlers is answered by `objdump` on the linked image, not by a PC.

What is left is small and it is the part the whole idea rests on: four faults
must map to four **different** counts, and every count must be small enough to
count by eye. Two faults blinking the same number carry no more information
than a dead board.

So the test pins the distinctness, the 2-to-9 range, and the exact mapping the
README and the header both document. A blink code you cannot look up is just a
blinking light.

The kinds live in a table rather than four separate assertions, so a fifth
fault vector added later has to be added there too, and the properties still
have to hold for it.

### `ota_frame_test`: do both sides of the update link build the same bytes?

The start of the over-the-air update chain: industrial-hmi writes an update
agent that flashes this board over the same serial cable, through a framed
binary protocol both repos own, `docs/protocols/uart-flash-v1.md` in
industrial-hmi (status AGREED, 2026-09-22). This binary tests the envelope,
`Src/ota_frame.c`, and it is a contract test in the same shape as the telemetry
one: its anchors are copied verbatim from the spec, not derived from this code.

The module has two headers on purpose. `ota_frame.h` holds the frame layout
and the encoder, which is all a sender needs. `ota_frame_parser.h` holds the
parser. Code that only transmits, like the ACK and NAK path, includes the first
alone and cannot even name the parser: a file that tries fails to compile,
checked once by hand. The parser's verdict on a frame, good or bad checksum,
travels beside the frame to the sink instead of inside it, so a sender is not
handed a field it would have to ignore.

| Anchor, from the spec | Why it is enough |
| --- | --- |
| CRC-16/CCITT-FALSE over `123456789` is `0x29B1` | several CRC-16s share the polynomial, and only the right one gives this |
| `A5 01 01 00 00 00 E9 CD`, INFO_REQ number 1 | pins the start byte, the byte order, and what the checksum covers |
| `A5 82 01 00 00 00 EB 01`, its ACK | the same, from the board's side |

The checksum covers TYPE to PAYLOAD and never the `0xA5`; including it gives
`DC 70` instead of `E9 CD`, so a wrong reading of the spec cannot pass.

The rest is the parser on a noisy line, which is where it would really fail.
Bytes before a frame are skipped, including the reset `0xFF` measured on this
board. And the resync rule, section 9 item 8 of the spec: a random `0xA5` is a
false start, shown by a length over 260 or a bad checksum, and the parser then
resumes from the byte AFTER it, re-scanning bytes it already holds. It never
skips `LEN` bytes, because a garbage `LEN` can reach 65535 and swallow the real
frames behind it. One test hides a real frame inside a false frame's body and
checks it still comes out.

Eight mutants, each a realistic mistake, are each caught by a named test:
checksum over the start byte, checksum read big-endian, skipping `LEN` after a
bad checksum, the length limit off by one, throwing a false start away whole
instead of resuming after it, not skipping leftovers after a frame, resuming
two bytes after a false start instead of one, and the checksum verdict
inverted. The mutation run also caught a bug in the test itself: one
assertion read the last captured frame at index `count - 1` with `count` at
zero, the binary crashed, the crash threw away the buffered failure lines, and
a killed mutant was first reported as surviving. `docs/uml/ota-frame.puml` has
the parser's flow.

On the board the two modules cost 510 bytes of code (64 for the CRC, 446 for
the parser), and the linked image grew by 592. No RAM yet: nothing on the
board creates a parser so far. One will take 270 bytes, a whole frame kept
raw, because the resync rule needs the bytes back.


### `ota_update_test`: does the board answer every message the way the spec says?

The second OTA piece is the state machine, `Src/ota_update.c`: what a message
means right now, whether it is allowed, what the board does, and what it
answers. Every rule is from sections 4 to 6 of the agreed spec: `DATA` only
after `BEGIN`, offsets that follow on exactly, a size that fits, `COMMIT` only
once every byte is in and the image CRC32 matches, `ABORT` leaving the running
image alone, and a session that ends after 10 s of silence.

Its logic is tested completely on a PC, before it reaches the board. That works
because everything that exists only on the board sits
behind a port of function pointers, the same way `telemetry` takes its reader
and its sink. In the test the inactive bank is a RAM array, the clock is a
variable moved by hand, and the UART is a capture buffer. The test also plays
the host: each message goes through the real encoder and the real parser into
the state machine, and each answer comes back out through a second real parser,
so the whole receive-and-answer chain is exercised, not the state machine alone.

Two rules are easy to get wrong and each has its own test.

**Nothing happens twice.** When an `ACK` is lost the host resends the same
frame, and the board must answer again without erasing or writing again. A
repeat is recognised by TYPE and SEQ together, not SEQ alone, because a
restarted host counts from 1 again. A `NAK` is never remembered, so a clean
resend after `NAK BAD_CRC` is judged afresh and accepted.

**The silence counts from the board's last answer, not from the last frame.**
At `BEGIN` the board erases a whole bank before it answers; that is its own
work and must not count against the host. One test makes the erase take 8 s
and the host answer 9 s later, 17 s after its own `BEGIN`, and the session must
still be open. The comparison is a subtraction, so it survives the 49-day clock
wrap, and the wrap test checks both before and after zero, because the broken
addition form fails before zero, not after.

Fifteen mutants, each a realistic mistake, are each caught. Three of them were
caught only after the tests were fixed: a repeat check by SEQ alone, a timeout
written as an addition, and a session that forgot to clear its memory. The last
one hid because the test ended its session with `ABORT`, whose own `ACK`
overwrote the memory it was meant to check.

On the board the state machine costs 1042 bytes of code and 52 bytes of RAM.
`docs/uml/ota-update.puml` has the states.

## CI

Every push and every pull request runs four jobs, none of which needs a board:

| Job | Question it answers |
| --- | --- |
| Host tests | does the logic still do what it claims |
| Cross-compile | does it still build **and link** for the real Cortex-M4 |
| Static analysis | does it still hold the line on naming, size and bug classes |
| Docs discipline | do the diagrams still validate, does Doxygen still run clean |

The cross-compile uses the same flags as STM32CubeIDE and performs a full link,
because a missing symbol or an overflowing section only shows up at link time.
The docs job enforces the rule that every new piece carries a diagram validated
with `plantuml -checkonly`, so the discipline is checked rather than remembered.

### Static analysis, and what it is not

`clang-tidy` runs with `WarningsAsErrors`, so the first finding turns the build
red. The config is in `.clang-tidy`, every check opted into explicitly and every
exclusion carrying its reason. It enforces `snake_case` in the `module_action`
shape the code already uses, a 50-line ceiling per function (the longest today
is 33), and the `bugprone` and `clang-analyzer` families.

**This is not a MISRA checker and this project does not claim MISRA
compliance.** MISRA is a paid standard whose real checkers are commercial tools.
A partial free approximation presented as compliance would be an overclaim, and
it would not survive one question at an interview. What this is: a
machine-checked discipline layer in the same spirit.

Two exclusions are worth knowing about, because both would otherwise look like
the tool is broken. `FixedAddressDereference` fires on every register access,
which is exactly what a hand-written register map does. `DeprecatedOrUnsafeBufferHandling`
asks for C11 Annex K functions that no toolchain here ships.

`docs/uml/quality-gates.puml` lays out all four gates and, more usefully, what
each one **cannot** catch.

## Pin map

| Pin | Role |
| --- | --- |
| PA2 | USART2_TX (AF7), wired to the ST-Link virtual COM port |
| PA5 | LED LD2 (output) |
| PC13 | USER button B1 (input, internal pull-up; low when pressed) |
| ADC1 ch17 | internal temperature sensor |
| ADC1 ch0 | VREFINT, the internal reference used to correct for VDDA |

Clock: the MSI reset clock (4 MHz), no PLL setup. USART2 BRR = 35.

## Build and flash

Built with **STM32CubeIDE** (managed build, `arm-none-eabi-gcc`). Import the
project, Build, then Run to flash over the on-board ST-Link. The Nucleo also
mounts as a mass-storage drive, so a `.bin` can be flashed by drag-and-drop.

## Watch the output

`serial-monitor.bat` opens a live serial monitor on COM3 @ 115200 (pass another
port as an argument). Press the button and watch the frames stream.

Every line carries the time it reached the PC, and the same lines are saved to
`logs/serial-<date>-<time>.log` (ignored by git), so a session can be read or
shared afterwards:

```
15:24:14.751  [FF]   (no line end)
15:24:14.970  equipment/0/state,on
15:24:14.970  temp,23.6
```

That is a real capture. Any byte that is not printable ASCII is shown as hex in
square brackets, never hidden, and square brackets mean nothing else: the
monitor's own notes, like `(no line end)`, use round ones.

The first line is a press of the black reset button, captured on 2026-09-22
with the firmware of that day: **every reset put exactly one byte on the line,
`0xFF`**, 46 resets out of 46, no other value.

That byte is gone now, and the monitor is what found it. A single clean `0xFF`
is what the line looks like after one short low pulse: a start bit, then eight
ones. `board_init` switched the TX pin, `PA2`, to alternate function mode
before choosing which function, so for a few instructions it sat on AF0,
which is not the UART. Writing the function select (`AFRL`) first and the
mode (`MODER`) second, the order ST's own `HAL_GPIO_Init` uses, took it from
46 out of 46 resets to 0 out of at least 6, with nothing else changed.

It mattered beyond the monitor. Nothing ended the line between that `0xFF`
and the first frame after a reset, so on the wire the frame arrived with a
stray byte in front of its sensor id. What the host's parser did with it is
industrial-hmi's side of the contract; from this side it no longer happens.
`docs/uml/uart-pin-order.puml` shows the window.

That separation is deliberate, and it is what made the measurement possible.
The first version of the monitor read whole lines as ASCII. Every byte above
`0x7F` became `?`, and bytes with no line end waited for the next `\n`, so
several resets in a row were shown glued to the front of the next frame as
`?????equipment/0/state,on`. That looked like one reset producing a burst of
random noise. Given the new capture, it was most likely five resets, one `0xFF`
each: the old output cannot be replayed to prove it. `docs/uml/serial-monitor.puml`
shows how the two cases are told apart now.

Two limits, both seen in the same captures. Bytes that arrive less than 100 ms
apart are one line, so a reset followed at once by a press shows as
`[FF]equipment/0/state,on`: the byte is still visible, just not on a line of
its own. And bytes sent while no monitor has the port open are held somewhere
between the ST-Link and the PC and delivered together when one opens, which is
most likely how three resets made before a session showed up as a single
`[FF FF FF]`. Count bytes, not lines, and read every log. This README first
said 35 resets, from one session never read and one count made by eye. A
line count then said 43, because `[FF FF FF]` holds three bytes on one line.
The logs held 46.

## Docs

- API reference: `doxygen docs/Doxyfile` (output in `build/doxygen/html`).
- Architecture diagram (HAL / app layering): `docs/uml/architecture.puml`.
- Sequence (button press to telemetry frames): `docs/uml/sequence-button.puml`.
- Contract test (how the frames stay pinned to the host): `docs/uml/contract-test.puml`.
- Counts to degrees (the conversion and its guards): `docs/uml/temperature.puml`.
- Quality gates (what each one catches, and what it cannot): `docs/uml/quality-gates.puml`.
- Debounce (a decision about time, not a pause): `docs/uml/debounce.puml`.
- FPU (the two switches, and what happens if you flip only one): `docs/uml/fpu-enable.puml`.
- Fault signal (what the board does instead of going quiet): `docs/uml/fault-signal.puml`.
- Serial monitor (reset noise versus a real frame, before and after): `docs/uml/serial-monitor.puml`.
- UART pin order (one 0xFF per reset, and the two-line fix): `docs/uml/uart-pin-order.puml`.
- OTA frame parser (one byte in, the resync rule): `docs/uml/ota-frame.puml`.
- OTA update state machine (the session, and what each message may do): `docs/uml/ota-update.puml`.
