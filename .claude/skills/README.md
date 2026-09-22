# Skills, and which of them are worth anything here

A skill is a set of written instructions Claude loads during a session, in
place of its default approach. Not code that runs on your machine: text that
changes how the work is done. Three kinds reach this repo.

**Checked on 2026-09-22.** This list is read by hand, so a skill added later
will not be in it until someone looks again.

## 1. Written here, in this repo

| Skill | What it is for |
| --- | --- |
| `deliver-piece` | The delivery checklist of one firmware piece: branch before the first commit, every gate, mutants, the SOLID review with the layering check, the board, the journal, the charter, the push command. |

Claude loads it on its own when a piece starts or is about to be committed.
You can also ask for it by name: `/deliver-piece`.

## 2. Typed as a command, when you want that specific pass

| Command | What it does | When it earns its keep here |
| --- | --- | --- |
| `/code-review` | A separate review of the current diff, a branch or a PR, hunting correctness bugs. Effort levels from low to max. `--fix` applies what it finds. | On every piece, as a second pair of eyes over the SOLID review. Most valuable on register code, where a test cannot reach. |
| `/simplify` | Quality only: reuse, duplication, complexity. It does not look for bugs. | When a piece has grown sideways, or before a piece that will build on top. |
| `/security-review` | A security pass over the pending changes. | Piece 7: option bytes, `RDP`, the bank switch, the rollback. Little to say about a UART parser. |
| `/init` | Writes a `CLAUDE.md` describing the codebase. | Only if we ever want the repo rules in one generated file. We keep them by hand instead. |
| `/loop` | Repeats a prompt on an interval, or self-paced. | Watching a long CI run, if we ever need it. Not for one-off work. |
| `/schedule` | Creates a scheduled agent that runs on a cron. | Nothing here needs it; the board and the journal are written when work happens. |

## 3. Enabled in Bogdan's claude.ai account, so they show up in every session

`docs`, `docx`, `pdf`, `pptx`, `xlsx`, `morning`, `import-memory`,
`skill-creator`.

Of these, only two are likely to matter for this work:

- **`pdf`** reads datasheets and reference manuals. RM0351 and DS10198 were
  read with `pdftotext` directly, which is fine; the skill is there if a PDF
  needs more than text extraction.
- **`skill-creator`** writes or improves a skill, including this one, when the
  checklist needs to change.

The rest produce Word, PowerPoint, Excel or a morning brief. They are not
wrong, they simply have nothing to do with firmware, and a piece of work here
should never end in a `.docx` when the journal and the board are the record.

## The rule that matters more than the list

A skill changes how Claude works. In this setup every rule that governs the
work is written down somewhere you can read and diff: `RULES.md` for the
shared rules, the workstream charter, the hooks, the board for anything that
touches another workstream. A skill that arrives from an account, unread, is
the one instruction that skips all of that. So: local skills live in the repo,
and anything that arrives from elsewhere gets read before it is trusted.
