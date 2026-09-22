---
name: deliver-piece
description: The delivery checklist for one piece of firmware work in this repo, from the branch before the first commit to the push command. Use at the start of a piece, before the first commit of a piece, and before telling Bogdan a piece is done. Covers the branch rule, the gates (gcc, clang-tidy, PlantUML, Doxygen, ARM link), mutants, the SOLID review with the layering check, the board, the journal and the charter.
---

# Delivering one piece in hmi-edge-node

Written after two commits landed straight on `main` on 2026-09-22, because a
habit is not a mechanism. Everything here has cost something once.

## Before the first line of code

1. **Branch first.** `git checkout -b <name>` before any commit, in every
   repo. The hook that guards `main` does not see a commit whose message comes
   from stdin (`git commit -F -`), so it will not save you.
2. **Read the source, do not remember it.** A hardware fact goes in only with
   its document and page: RM0351 Rev 9 for registers and sequences, DS10198
   Rev 8 for timings, the SVD in the CubeIDE plugins for addresses and bits,
   `uart-flash-v1.md` for the protocol. If a manual is missing, ask before
   downloading, and name the source and size.
3. **Name the risk.** What is irreversible here? In this project only option
   bytes are (RDP level 2). Anything else is recoverable by reflashing, and
   saying which is which out loud changes how the code gets written.

## While writing

- **Layers, checked on `#include`s, not by eye.** HAL (`core`, `board`,
  `uart`, `adc`, `flash`, `crc_unit`, `registers.h`) never includes an app
  header. App (`telemetry`, `temperature`, `ota_frame`, `ota_update`) touches
  no register. Utilities (`byte_ring`, `crc16`, `crc32`, `le_bytes.h`) depend
  on nothing. Composition (`main.c`, `ota_port`) is the only place the two
  meet.
- **No magic values**, including in tests: a number, a string or a bare bool
  gets a name. clang-tidy only catches numbers.
- **A contract question is proposed on the board, never invented quietly.**

## The gates, all of them, before the commit

```
mingw32-make -C tests run CC=gcc          # CI uses gcc; clang too is welcome
clang-tidy over Src/*.c and tests/*.c     # same loop as .github/workflows/ci.yml
java -jar plantuml.jar -checkonly docs/uml/*.puml
doxygen docs/Doxyfile                     # zero warnings
arm-none-eabi-gcc ... -T STM32L476RGTX_FLASH.ld Src/*.c Startup/*.s   # a real link
```

**A green local lint is evidence, not proof.** On Windows `unsigned long` is
32 bits and on the Ubuntu runner it is 64, so
`bugprone-implicit-widening-of-multiplication-result` can be silent here and
red there. A constant that takes part in an address is written `UL`.

## Mutants

Each mutant is a realistic mistake, applied with an exact-text replacement.
**Check that the mutation actually applied** (compare with the original) and
**decide by the exit code**, not by the printed text: a crashing test prints
nothing and looks like a pass.

## The review, in the journal or the PR

One line each for S, O, L, I and D, saying how the piece meets it or why the
letter does not apply, plus the layering check, plus a line naming the weakest
point of the piece and what would improve it. "Nothing" is not an answer.

## Proving it on the board

- Ask Bogdan to Clean, Build, then Run. Never trust a binary you did not check:
  compare the ELF time against the sources, look for the new symbols, and read
  the instructions when the order matters.
- Measure instead of estimating, and write the number down with its conditions.
- Every claim needs its control run: a good CRC32 AND a deliberately wrong one,
  because otherwise a board that stored nothing looks like one that stored
  everything.

## After it works

1. **Journal** (`portfolio-journals/firmware/ota-target.md`): a chapter with
   the decisions, the mistakes as they happened, the cost on the board, the
   review, the glossary terms and the quiz questions, then the balance row.
2. **Board** (`board/firmware.md` only): what changed for another workstream,
   in English, with `AFFECTS` and the contract effect. Never edit another
   workstream's file.
3. **Charter** (`firmware.md`, the state section): the piece, its figures and
   what remains.
4. **Push command**, always with the `cd` in it:

```bash
cd /c/Users/Bebe/STM32CubeIDE/workspace_2.2.0/nucleo-serial-button && git push -u origin <branch>
```

5. **After the merge**, verify it: `git pull` (read the real exit code),
   `git log main..<branch>` must be empty, then delete the branch, close the
   journal balance and update the charter.

## Language

Romanian to Bogdan, English in the repo and on the board. No em-dash anywhere.
Commits and board entries carry no attribution footer.
