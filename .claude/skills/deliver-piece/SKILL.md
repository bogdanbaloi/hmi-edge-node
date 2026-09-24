---
name: deliver-piece
description: The delivery checklist for one piece of firmware work in this repo, from the branch before the first commit to the push command. Use at the start of a piece, before the first commit of a piece, and before telling Bogdan a piece is done. Covers the branch rule, the gates (gcc, clang-tidy, PlantUML, Doxygen, ARM link), mutants, the SOLID review with the layering check, the board, the journal and the charter.
---

# Delivering one piece in hmi-edge-node

Written after two commits landed straight on `main` on 2026-09-22, because a
habit is not a mechanism. Everything here has cost something once.

## Before the first line of code

1. **Branch first.** `git checkout -b <name>` before any commit, in every
   repo. The hook that guards `main` does catch this, in every form of the
   command, since hub fixed its segment builder on 2026-09-24. **Branch first
   anyway.** This file used to blame `git commit -F -` for the two commits that
   landed on `main` here, which was wrong: hub could not reproduce that, and
   the real hole was a command written on several LINES, where everything
   below the first line was invisible to the whole hook. A guard that was
   silently blind for days is a reason to keep the habit, not to lean on it.
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

## The gates, before the commit

The list below is the WHOLE of what CI checks, job by job, so a local pass
means the same thing the runner will mean. It said "all of them" until
2026-09-25 while covering four of the five jobs, which is why every entry now
names the job it stands for.

```
mingw32-make -C tests run CC=gcc          # job "Host tests". CI uses gcc
arm-none-eabi-gcc ... -T STM32L476RGTX_FLASH.ld Src/*.c Startup/*.s
                     # job "Cross-compile for Cortex-M4", a real link
arm-none-eabi-gcc ... tests/oversize_image.c
                     # same job: this link MUST FAIL, "region FLASH overflowed"
sh scripts/check-public-text.sh <base>..HEAD    # job "Public text"
clang-tidy over Src/*.c and tests/*.c     # job "Static analysis", same loop
java -jar plantuml.jar -checkonly docs/uml/*.puml    # job "Docs discipline"
doxygen docs/Doxyfile                     # same job, zero warnings
```

**What these do NOT check, said out loud every time a piece is reported done.**
A tool that gives a go-ahead it cannot support is worse than one that gives
none, because the next reader quotes the verdict instead of the evidence.

- **The board.** Nothing above runs on hardware, and the last three real bugs
  here were invisible to every one of these gates: a write into a locked
  register, a vector table inherited from the boot loader, and a last byte cut
  off by a reset.
- **A pull request title and description.** The `public-text` job sees them,
  no local command does, so those stay a self-scan until the PR exists.
- **Whether the mutants still kill.** That is a separate run, below.
- **The journal, the board entry and the charter.** No gate has an opinion
  about them, which is exactly why they are numbered steps and not reminders.

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
