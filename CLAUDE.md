# Working on `swtpcsim`

A C++20 simulator of Motorola 6800 machines: the SWTPC 6800 on the SS-50/SS-30 bus, and the
MITS Altair 680b. No dependencies beyond a C++20 compiler and CMake; SDL3 is optional and
detected, never required. (The project began as `altairsim` and keeps that framework; see
`DESIGN.md`'s heritage note.)

## How work is done here

**Every task follows two skills: `work-task` (plan → build → review) and `ship-change`
(commit → PR → merge → issues).** Load the one that fits before acting. The rules hold
whether or not a skill is loaded:

1. **No work without a plan made in plan mode** and approved. No edits before approval.
   A feature plan first shows the feature is needed — not already documented, not a docs gap.
2. **Tests never work around a bug.** A test that exposes a bug gets the code fixed. Never
   loosen an assertion, skip the test, dodge the input, or add a retry.
3. **A bug related to the task is fixed in the same change. An unrelated bug gets a GitHub
   issue** at once (signed `--SWTPCSim Claude`) and is left alone.
4. **Docs are updated before the commit**, as part of the change under review.
5. **No commit until the maintainer has reviewed and approved it.** Stop and report the diff.
6. **No PR until the maintainer approves opening one.** More commits may come first.
7. **No merge until CI is green** on all three platforms. Then merge without asking again.
8. **After the merge, comment on the related issues.** Who closes one depends on who opened
   it: an issue opened by `deltecent` is closed by `Fixes #N` in the PR. Anyone else's is never
   closed by us — `Refs #N`, no closing keyword — and the person who opened it closes it.

The three gates are **plan approved**, **commit approved** and **PR approved**. Each
approval covers that one step only; it never carries over to the next. **A PR from anyone but
`deltecent` goes through the `review-pr` skill: its merge needs green CI *and* the
maintainer's approval.** **An incoming issue goes through the `review-issue` skill**, which
reaches a verdict and stops there — it never builds anything.

**A report that hands the next move to the maintainer ends with it.** The summary comes first;
the last line says what is ready and what it is waiting on (*"Ready for your review on branch
`fix/x`. Nothing is committed or pushed."*). That holds at every gate, in every skill.

## If you are here to build or ship a release

**Read [`DISTRIBUTION.md`](DISTRIBUTION.md) and follow it.** It is written to be executed
step by step on a machine that has never seen this repository — literal commands, the exact
output to check after each, and a STOP condition on every check.

**If you are on the Intel Mac, the Windows box, or the Linux box, your job is §4.2 and
nothing else.** Build, test, package, upload to the draft release. **A build machine never
tags, never publishes, and never decides a version number.** If a check in §4.2 fails, stop
and report it — do not work around it and do not judge it probably fine. Nothing gates a
package after you upload it.

**On Windows, you do not need a Developer shell.** With CMake's default Visual Studio
generator, MSBuild finds the toolchain itself — a plain PowerShell works. Remember that your
own environment does not survive between commands: if you use Ninja instead, set up the
environment *within* the same command (`cmd /c "call vcvars64.bat && …"`).
`DISTRIBUTION.md` §4.4 has the table. **MSVC is the only supported Windows toolchain.**

**Your environment does not carry over from the terminal that launched you** — each command
runs in a fresh process, so a Developer PowerShell's `vcvars` setup never reaches your build
commands. There is no "set it up once at the top". Use the Visual Studio generator, which
needs no environment at all.

> **First time on the Windows box? Start with `docs/building-windows.md` §6.** It is a job,
> not a description: three build approaches that are *believed* to work and have never been
> run here, with the commands to settle each and how to report what you find — including
> `tools\build-sdl3-static.bat`, which was written from the working shell script and never
> executed. Do that before attempting a release build, and if something fails, report it
> rather than working around it. A failure there is the point.

## Before changing anything else

| | |
|---|---|
| [`DESIGN.md`](DESIGN.md) | The architecture and the reasoning. **Read the relevant section before implementing** — most surprises here are deliberate and explained. |
| [GitHub issues](https://github.com/deltecent/swtpcsim/issues) | The public work list: bugs and features. Check before proposing something — a declined idea may be recorded here with its reason so it does not get re-raised. (`TODO.md` is the maintainer's local, **untracked** brainstorming scratch and is not in the tree.) |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and shipped. |
| `docs/manual/` | The User Manual, for someone holding a release package. |
| `docs/devguide/` | The Developer Guide, for someone changing the source. |

## Rules that bite

- **Every change goes on a branch off `master` and lands through a PR** — see *How work is done
  here* above.
- **`TODO.md` is untracked** — a local, fast-moving working doc, not in the tree. Its edits
  never go through git, so they need no branch and no PR. Anything in it meant for the public
  becomes a GitHub issue instead.
- **The word is "board", not "card"** (`DESIGN.md` §0.3), for the object, the command and
  the table. *Card* only where the sentence is genuinely about the physical 1970s artifact.
- **`docs/manual/ref/` is GENERATED from the binary.** Edit the emitter and run
  `cmake --build build --target docs-reference`; never hand-edit those files.
- **The manual may not name anything outside the package** — no source paths, no
  `CMakeLists`, no `ctest`, no `DESIGN.md`. `tests/acceptance/docs-manual.cmake` enforces
  this, so a chapter that cites the repository fails the build. The Developer Guide is
  allowed to, and is not checked.
- **A new manual chapter must be added to `docs/manual/ORDER`**, or it is written,
  committed, and silently not in the PDF.
- **The PDFs are built and committed by CI** (`.github/workflows/docs.yml`, pandoc pinned at
  3.6). If you run `tools/build-docs.sh` locally, `git checkout --` the PDFs afterwards —
  a local pandoc is a different pandoc, and a different pandoc is a different document.
- **Never give hardware a behavior it never had** to fix a software symptom. Check the host,
  the filter and the monitor layers first.
- **`<!-- @claude ... -->` in a Markdown file is a review comment for you.** The annotated file
  is usually a *copy kept outside the repo* that shares the master's basename; match it to the
  repo file of that name (ask when the name is ambiguous) and revise the **master** to address
  each note. Never leave a marker in a `docs/manual/` chapter — its raw text is grep-checked.
  Full convention: `docs/devguide/doc-review-comments.md`.

## Research economy

- **Before spawning Explore/Plan agents, check the memory index and run one
  targeted `grep`/`Read`.** This repo carries a lot of durable knowledge already —
  `DESIGN.md`, `docs/devguide/`, and the maintainer's memory pointers. Fan out
  agents only when scope is genuinely unknown or spans many unfamiliar subsystems.
- **After tracing something non-obvious, write it back** — a memory note
  (`file:line` + conclusion) for working state and gotchas, or `docs/devguide/`
  when the fact is about how the code is built. That turns a future trace into a
  one-line recall.

## Testing

```sh
./build/swtpc_tests <names>         # local loop: the suites for the subsystem you touched
                                    #   e.g. ./build/swtpc_tests mps dc4 cpu6800
                                    #   ./build/swtpc_tests --list  to see the names
ctest --test-dir build              # optional full local run  (15 tests, ~15 seconds)
```

**Local cadence: run the unit suites for what you changed, then commit — CI runs the full
suite.** `swtpc_tests <names>` runs only the named suites (no args = the full suite,
exactly as `ctest` invokes it); a mistyped name is a hard error, not an empty pass. There is
no required full local run before a commit: CI runs the full suite on three platforms on every
push and is the backstop. Selection rests on *your* judgment of what a change touches, so
the impacted-test list can be wrong — that is what CI catches. Run the full local `ctest`
yourself when you want the answer before pushing (a wide or cross-cutting change), or for
release-ish work.

**Match the pass line, not the absence of errors** — read `100% tests passed out of N`. A
`cd` that leaves the build directory can make `ctest` not run at all, which looks identical
to success if you are only checking for the word "error".

**Warnings fail CI (all three toolchains).** Every CI leg configures with `-DWERROR=on`, which
adds `-Werror` on GCC/Clang and `/WX` on MSVC, so a warning on any of them reds a PR before
merge. It is off by default locally; reproduce the gate before pushing a code change with
`cmake -B build -DWERROR=on && cmake --build build -j`. GCC/Clang run `-Wall -Wextra -Wpedantic
-Wshadow` (plus `-Wshadow-uncaptured-local` on Clang, so a Mac build catches the lambda-local
shadowing GCC flags); MSVC runs `/W4` with two intentional classes suppressed tree-wide (`/wd4244 /wd4267`,
the 8-bit emulator's integer narrowing — issue #238 closed that backlog). Because macOS is the
only leg that builds SDL3 and MSVC skips it, a warning in SDL-guarded code only reds macOS.

The acceptance tests are not smoke tests: each boots real period software on a whole machine
through the real CLI and reads back what landed on the terminal.

## Driving a running guest — use `--mcp`, never hand-roll expect

**Any time you need to type at a running guest and read what it prints — boot FLEX, run a
FLEX utility, or just verify a machine comes up to SWTBUG's `$` and a command works — drive it
with `swtpcsim <machine> --mcp`.** This is a core capability of the simulator; reaching for it
is not optional. Do **not** write a throwaway `expect`/pty script to poke a guest interactively
— that fights console pacing and recurring prompts, and it is the exact wheel you keep
reinventing. `expect` under `tests/acceptance/*.exp` is **only** for a committed acceptance
test, where the pty harness *is* the deliverable.

The recipe: line-delimited JSON-RPC on stdin (`initialize` → `notifications/initialized` →
`tools/call`); MCP does **not** run the machine file's `startup>`, so boot yourself with
`run {from:<ROM/monitor addr>}`, then `run {input:"CMD\r", until:"<string unique to the
state you want>"}` per command. Never pick an `until` that is a prompt which recurs (FLEX's
`+++` and SWTBUG's `$` both reprint, so match on the text a command *produces*, not the prompt
it returns to). See `docs/manual/mcp.md`.
