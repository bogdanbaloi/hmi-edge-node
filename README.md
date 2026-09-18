# hmi-edge-node

Bare-metal STM32 firmware for a **Nucleo-L476RG** (ARM Cortex-M4). It is the
device side of a hardware/software integration demo: it reads the physical
world (a button, the MCU's internal temperature sensor) and streams telemetry
over a serial link to the [industrial-hmi](https://github.com/bogdanbaloi/industrial-hmi) host, which
toggles a production line in its dashboard.

Written **register-level, no HAL and no CMSIS** -- every peripheral is brought
up by hand from the reference manual (RM0351). The register map lives in
`Inc/registers.h`.

## What it does

On each press of the USER button (B1) it sends two lines over USART2:

```
equipment/0/state,on\n      (toggles on each press)
temp,<raw>\n                (internal temperature sensor, raw 12-bit ADC counts)
```

The on-board LED (LD2) mirrors the equipment state. Warm the die and the `temp`
value rises.

## The link to industrial-hmi (a protocol, not code)

The two repos are independent. They interoperate through a **serial wire
protocol** -- `sensorId,value\n` lines at 115200 8N1 -- the same way a browser
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
| HAL / BSP | `board`, `uart`, `adc`, `registers.h` | the only code that touches registers |
| App | `telemetry` | pure logic: a button edge -> the telemetry frames |
| Composition | `main.c` | wires the HAL to the app and runs the loop |

`telemetry` depends on injected function pointers (a temperature reader and a
line sink), so a host test drives it with a fake reader and a capturing sink --
no board needed to test the frame logic.

## Contract test

The wire protocol is the one thing two independent repos have to agree on, and
nothing in a compiler checks it. `tests/` closes that gap: a host-compiled test
that fails the build if this firmware stops emitting what the host can parse.

```
mingw32-make -C tests run
```

No board, no test framework, no dependency on the other repo -- it compiles the
pure-logic `Src/telemetry.c` with the desktop compiler already on PATH.

It is built in two layers, and that shape is the point:

| Layer | What it pins | Fails when |
| --- | --- | --- |
| `frame_gate` | the host parser's acceptance rules, driven by samples copied verbatim from its `SerialFrameParserTest` | the transcribed rules drift from the host's |
| `contract_frame_test` | telemetry's real output, pushed through that gate and asserted byte for byte | this firmware changes the frame format |

With literal assertions alone, a format change could be "fixed" on both sides at
once and the contract would break in silence. The gate is anchored to the host's
own samples, so it cannot be edited into agreement.

Covered: the two frames per press and their order, the on/off toggle, the press
edge (a held button emits nothing), and the hand-rolled decimal formatting
across the range of `uint32_t`.

## Pin map

| Pin | Role |
| --- | --- |
| PA2 | USART2_TX (AF7), wired to the ST-Link virtual COM port |
| PA5 | LED LD2 (output) |
| PC13 | USER button B1 (input, internal pull-up; low when pressed) |
| ADC1 ch17 | internal temperature sensor |

Clock: the MSI reset clock (4 MHz) -- no PLL setup. USART2 BRR = 35.

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
