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
line sink), so a host test drives it with a fake reader and a capturing sink --
no board needed to test the frame logic.

`adc` reports counts and the factory constants but deliberately does **not**
convert to degrees: that is arithmetic, and it lives in `temperature` where a
host test can reach it. The adapter that joins the two sits in `main.c`, the
only place a driver and pure logic are supposed to meet.

`core` is separate from `board` on purpose. `board` owns what changes when you
swap the board; `core` owns the CPU itself, which is the same on every
Cortex-M4 and is described by the ARM architecture manual rather than by
RM0351.

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

## Tests

```
mingw32-make -C tests run
```

No board, no test framework, no dependency on the other repo. It compiles the
pure-logic sources with the desktop compiler already on PATH. Two binaries, two
different questions:

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

## CI

Every push and every pull request runs three jobs, none of which needs a board:

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

## Docs

- API reference: `doxygen docs/Doxyfile` (output in `build/doxygen/html`).
- Architecture diagram (HAL / app layering): `docs/uml/architecture.puml`.
- Sequence (button press to telemetry frames): `docs/uml/sequence-button.puml`.
- Contract test (how the frames stay pinned to the host): `docs/uml/contract-test.puml`.
- Counts to degrees (the conversion and its guards): `docs/uml/temperature.puml`.
- Quality gates (what each one catches, and what it cannot): `docs/uml/quality-gates.puml`.
