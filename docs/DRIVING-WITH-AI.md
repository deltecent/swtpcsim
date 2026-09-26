# Driving swtpcsim with an AI

How to let an **AI assistant** drive the **swtpcsim** Motorola 6800 simulator through its
built-in MCP server — so you can say *"using swtpcsim, do …"* and it does it, instead of you
cutting and pasting between a chat window and a terminal. It can boot a machine, type at its
console, read what it prints, inspect and single-step a program, debug one, and talk to a
program over a real serial port — all through typed tools.

This works with **any MCP-capable assistant**. The examples below use **Claude** as the concrete
client, but the server speaks the open Model Context Protocol and the same steps apply elsewhere.

Every recipe below was **verified end to end** against the FLEX machine that ships in this
package (`examples/flex/flex2-40.toml`): the boot-to-`+++` and the serial attach both run
green. Point at the `swtpcsim` you were given.

**New to this?** `examples/flex/` is a ready-made working directory: register the server
there (below) and ask your assistant to boot FLEX and list the disk — a complete, guided round
trip through everything this document describes.

## Starting the server

```
swtpcsim <machine> --mcp        # <machine>: a built-in name, or a path to a .toml
```

It speaks line-delimited **JSON-RPC 2.0 on stdio**. Send `initialize`, then `tools/call`.
A machine named on the command line is loaded (disks mounted, boards fitted) but **its
`startup` is NOT run** — under `--mcp` you boot it yourself with the `run` tool, so nothing
blocks before you have control. Switching machines mid-session with `CONFIG LOAD` is safe the
same way: its `startup` runs up to the boot `RUN`, which under `--mcp` **parks** the PC rather
than entering the run loop — so `CONFIG LOAD anymachine.toml` never wedges the server. Advance
it with `run {from: …}` afterward.

`swtpcsim --list` shows the built-in machines. The FLEX example this guide is written
against is the machine file `examples/flex/flex2-40.toml`.

Minimal driver:

```python
import subprocess, json
p = subprocess.Popen(["swtpcsim", "examples/flex/flex2-40.toml", "--mcp"],
                     cwd=WORKDIR, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     text=True, bufsize=1)
# write {"jsonrpc":"2.0","id":N,"method":"tools/call","params":{"name":..,"arguments":..}}\n
# read one JSON line back per call.  results are in result.structuredContent
```

`cwd` matters: relative paths — a machine file, a disk image you `MOUNT` — resolve against the
directory you launch from, so launch from the directory holding the files you want to reach.

## Register the server with your assistant

Starting the server by hand is only for a quick look. To have the **assistant** drive it, you
register `swtpcsim --mcp` as an MCP server with your client **once**, and from then on you just
talk to the assistant. A client needs two things: the command to run, and to know it speaks over
stdio — both of which `swtpcsim --mcp` satisfies.

**Register it from the directory your files are in** — a relative machine path or any image you
mount resolves against wherever the server is launched, so launch it where your files are. If
`swtpcsim` is not on your `PATH`, use its full path in place of `swtpcsim` below.

**Claude Code (the CLI).** One command, with the machine you want it to drive after the `--`
(everything after `--` is the command the client will run):

```
cd examples/flex
claude mcp add swtpcsim -- swtpcsim flex2-40.toml --mcp
claude mcp list                       # confirm it is registered and reachable
```

Then start `claude` in that directory and give it the job in plain language:

> *Using swtpcsim, boot FLEX and show me what is on the disk.*

`claude mcp add` defaults to **local** scope (this project, just you). Add `--scope project` to
write a shareable `.mcp.json` into the directory instead — commit that and anyone who opens the
folder gets the same server. `claude mcp get swtpcsim` shows how a given one is configured.

**Claude Desktop, or any other MCP client.** These read a JSON config. Add an `mcpServers` entry
naming the command and its arguments:

```json
{
  "mcpServers": {
    "swtpcsim": {
      "command": "swtpcsim",
      "args": ["/absolute/path/to/examples/flex/flex2-40.toml", "--mcp"],
      "cwd": "/absolute/path/to/examples/flex"
    }
  }
}
```

**Use absolute paths here.** The Desktop app currently launches a server in your **home
directory**, not the project (a known Claude Code bug), so a relative machine path won't be found
and a relative disk won't land where you expect. Give the machine an absolute path and set `cwd`
to the folder your files are in. On macOS, Claude Desktop's config is
`~/Library/Application Support/Claude/claude_desktop_config.json`; **restart the app** after
editing it. Other clients differ in *where* the config lives, but the `mcpServers` block is the
same shape.

## Several machines, and several projects

Nothing above is one-machine or one-project — the same mechanism scales three ways.

**The server name is a label, not the machine.** In `claude mcp add `**`swtpcsim`**` -- …`, the
first word names the *server*; register as many as you like under different names and an assistant
sees them all at once, each pointed at its own machine:

```
claude mcp add swtpc-flex -- swtpcsim flex2-40.toml --mcp
claude mcp add altair680  -- swtpcsim altair680     --mcp
```

(Names are letters, digits, `-` and `_`.)

**Scope keeps projects apart.** The default **local** scope files the server under the directory
you ran `claude mcp add` in, so it shows up only when you start `claude` there — register swtpcsim
once per project, in that project's folder, and they never collide. **`--scope project`** instead
writes a `.mcp.json` into the folder, so the server travels with it (commit or copy the folder and
it comes too) — the right choice for a self-contained machine directory like `examples/flex/`.
**`--scope user`** makes one entry for every project, which suits a fixed machine that needs no
project files (a built-in like `altair680`).

**The working directory is where your files are.** A relative `<machine>` path and any disk you
mount resolve against the *server's* working directory. From the **`claude` CLI** that is the
directory you started `claude` in, which is why registering and running from the machine's own
folder just works, and why relative paths in a committed `.mcp.json` stay portable (use
`${CLAUDE_PROJECT_DIR}` in the paths to be robust even when `claude` is started from a subfolder).
The **Desktop app** is the exception noted above — it starts the server in your home directory —
so there, use absolute paths.

## Watching over its shoulder — and taking the keyboard

You do not have to read a transcript after the fact to see what the assistant is doing. Add
`--mirror socket:PORT` next to `--mcp` and a person can `telnet localhost PORT` to watch the
**very session the assistant is driving** — every character the guest prints as it prints it —
and **type back onto the line to take over**, sharing the console with the assistant:

```
swtpcsim examples/flex/flex2-40.toml --mcp --mirror socket:2323
```

The assistant keeps driving through `run`/`send`/`recv` exactly as before — the mirror is
invisible to it — while whatever it types and whatever the guest prints also crosses the socket to
you. Type at your `telnet` and the guest reads it as if you had reached over and used the keyboard.
Add `?ro` to watch without being able to type — quote it (`--mirror 'socket:2323?ro'`), since
`?` is a shell wildcard and an unquoted `socket:2323?ro` makes the shell fail with `no matches
found`. One watcher at a time.

The guest only advances **during a `run`**, so a character you type between the assistant's runs
waits on the line and is read on its next `run` — the same as staging input with `send`. While a
`run` is in flight you and the assistant share the console live. (This is the same `|socket:PORT`
mirror the monitor's `CONNECT` offers on any line; the *Serial lines* chapter of the User Manual
covers it in full.)

## The tools

`tools/list` is authoritative — it returns the tools this build offers. Each tool's schema comes
off the board itself, so ask `board_types` what a board can be told rather than guessing.

**Build / inspect a machine:** `board_types`, `board_list`, `board_get`, `board_add`,
`board_set`, `who`, `bus_map`, `bus_io`, `bus_contention`, `mem_dump`, `mem_deposit`,
`mem_load`, `roms`, `reset`.

**Drive a running guest:**

| Tool | Args | Does |
|---|---|---|
| `run` | `from?`, `input?`, `until?`, `timeout_ms?` (2000, max 600000), `max_steps?` | Type `input`, advance the guest, return what it printed. Stops on `until` match, a **prompt** (guest idle on console input), `timeout_ms`, `max_steps`, WAI, a breakpoint, an address no board decodes under `SET BUS UNCLAIMED=HALT` (`unclaimed`), a `BREAK TAPE STOP` (`tape-stop`), or a cancel of the request (`notifications/cancelled`) or a ^C sent to the swtpcsim process (both give `stopped: "interrupted"`) — see `stopped`. `timeout_ms` is a ceiling, not a wait: the call returns as soon as one of the others fires. `from` sets PC first (that is how you boot). Bus and board messages from the run, such as a `SET BUS UNCLAIMED=WARN` line, come back in `warnings`. **Never blocks.** |
| `send` | `text` | Type at the console without running. |
| `recv` | — | Drain output since last read, without running. |
| `regs` | — | CPU registers now (`pc`, `halted`, `registers{}`). |
| `status` | — | Answered at once, even mid-`run`: `in_flight`, and the `pc`/`steps` of the last `run` (see below). |

**`monitor`** `{command}` runs any one monitor command (`CONNECT`, `MOUNT`, `SET`, `DUMP`,
`DISASM`, …) and returns its text — the escape hatch for anything without a dedicated tool.

## Knowing the commands

You do not have to memorize the monitor. Two ways to get the whole surface:

- **`cheatsheet.md`, shipped beside this file** — the full `swtpcsim [options]` block, every
  monitor command with its abbreviation and usage, every board and machine, the `CONNECT`
  endpoint table, and a machine-file skeleton. It is generated from the program, so it matches
  the binary you were given. Read it once for the lay of the land.
- **Ask the running machine.** `monitor {command: "HELP"}` lists every command;
  `monitor {command: "HELP <cmd>"}` prints one command's abbreviation, usage and detail
  (`?` is the same as `HELP`). For the MCP/board surface, `tools/list` and `board_types`
  self-describe.

## The pattern: an expect loop

One `run` per guest command, matching the prompt each time:

```
run {from: 0xE0D0, until: "$"}                   # boot SWTBUG to its $ prompt (its RESET entry)
run {input: "D", until: "?"}                     # SWTBUG's D boots FLEX; it asks for the DATE
run {input: "1,15,80\r", until: "+++"}           # answer, and FLEX reaches its +++ prompt
run {input: "CAT\r", until: "+++"}               # a command, read the reply
```

`\r` submits a FLEX line. `run` also returns on its own when the guest reaches a prompt
(`stopped: "idle"`), so you rarely need to guess a timeout for interactive commands — set a
generous `timeout_ms` only for long silent work (a disk load, a long assembly).

**`from` is a JSON number, and JSON has no hex.** The `0xE0D0` above is shorthand; on the wire
write the decimal value: `57552` for `E0D0`, `65496` for `FFD8`. A string such as `"0xE0D0"` is
not a number, so the server refuses the call and tells you the number to send: `` `from` must be
a JSON number, not a string: "0xE0D0" is 57552 ``. Every tool checks its arguments this way, so a
wrong type or a missing required argument is an error, and never a silent 0.

### Stopping a `run` early, and `status`

A `run` ends by itself at `timeout_ms`. Two things stop it sooner, and both return
`stopped: "interrupted"` with what the guest printed so far:

- **Cancel the request.** Send the standard `notifications/cancelled` with the request id of the
  `run`. The server reads its input while a `run` goes on, so it sees the cancel at once. A cancel
  that names another request, or that arrives after the `run` returned, is ignored, and it never
  applies to the next call. Other requests sent during a `run` wait, and are answered in order after
  it returns.
- **Send the process a `^C`** (`kill -INT`). The first `^C` is caught and only interrupts the `run`.
  A second `^C` before the server has reported the first one ends the server. A server started in
  the background, or with `nohup`, ignores `^C`.

Either way the machine is left as it was, and the next `run` starts clean. A `^C` with no `run` in
progress does nothing to the guest.

**`status` never waits.** It is answered by the reader thread, not the worker, so it answers even
while a `run` (or a long `monitor`, `mem_load` or `snapshot`) is in progress. It returns the board
id, `in_flight` (whether the worker is busy on any request), and the `pc` and `steps` of the last
`run`. Those two go stale: a `step` or a `monitor` command moves the real PC without changing them,
and `steps` restarts at zero on the next `run`. `generation` is the one field that always climbs, so
use it to tell "still advancing" from "stuck on the same slice". Poll `status` before you cancel, to
see whether the `run` is still alive.

## Recipe: boot FLEX and work with the disk

Machine: the FLEX config (`examples/flex/flex2-40.toml`). It fits a SWTPC 6800 with
SWTBUG in ROM and a DC-4 floppy controller carrying the FLEX 2.0 boot disk on `dc40:drive0`.

```
run {from: 0xE0D0, until: "$"}                          # SWTBUG's $ prompt
run {input: "D", until: "?"}                            # boot FLEX off the DC-4 (asks the date)
run {input: "1,15,80\r", until: "+++"}                  # -> FLEX 2.0, the +++ prompt
run {input: "CAT\r", until: "+++"}                      # the directory
run {input: "CAT 0.SYS\r", until: "+++"}                # one drive, one extension
```

`CAT` ends with a `SECTORS LEFT` line. FLEX's own commands (`CAT`, `LIST`, `COPY`, the editor
and assembler on a fuller disk) run the same way — one `run` per line, matching `+++`.

**Boot from the RESET entry, not a banner.** Under `--mcp` a machine's `startup` does not run,
and a pending RESET does **not** fetch its vector for you — so pass `from:` the monitor's own
RESET entry (SWTBUG's is `0xE0D0`; MON680's on `altair680` is `0xFFD8`) rather than expecting a
sign-on line. The first `run {from: …}` is the boot.

**Confirm the guest is at its prompt before you type.** FLEX's console is a single 6850 with a
one-byte receiver, so input sent while it is still printing (the sign-on, a long `CAT`) can
overrun and be swallowed. Loop `run` until the guest is idle **at** the prompt you expect —
`+++` for FLEX, `$` for SWTBUG, `.` for MON680 — *before* you feed the next line.

**Work on a copy if you will write.** The shipped `FLEX2-40.DSK` is mounted **read-only**, so a
`SAVE`/`COPY` onto it fails rather than corrupting it. To write, copy the folder first and drop
the read-only flag on the mount (`mount = { file = "FLEX2-40.DSK" }` without `readonly = true`),
or `MOUNT` a scratch disk on another drive. swtpcsim commits a writable image to the host `.DSK`
on **`UNMOUNT`** (or `QUIT`), so end at the `+++` prompt and `UNMOUNT` before you read it back.

## Investigate a program you did not write

Taking a binary apart to see how it works — a monitor loader, a period utility, a game — is what
the machine is really for. The moves that take a 6800 program apart are `SYMBOLS LOAD prog.PRN`
(so `DISASM` reads `JSR BEGIN`, not a bare address), `BREAK <addr>` at the entry point, then
`DISASM` the region and `STEP` through it — `NEXT` over the calls you already trust.

The pattern for "explain what this does": get the code into memory, `BREAK` where you want to
start looking, `DISASM` the region, then `STEP` through the interesting part reading the
registers — the same way you would at a front panel, but with the assistant doing the
bookkeeping. On `altair680` you can do this against MON680 itself: `run {from: 0xFFD8, until: "."}`
to its `.` prompt, then `monitor {command: "DISASM FF00 20"}` to read the monitor's own code. The
full command set with a worked session for each is the **Debugger** document
(`swtpcsim-debugger.pdf`), and every command's syntax is in `cheatsheet.md` beside this file.

## Debugging a behavior: make the machine show you, don't guess

**Read this before you form a single theory.** When a guest misbehaves — a character dropped, a
byte mistimed, a loop that runs when it shouldn't — **the simulator already knows exactly what
happened. Get it to tell you before you decide what it is.** The debugger records every
instruction with its registers and every bus cycle; a breakpoint plus a history dump *shows* you
the cause. A hypothesis about what the guest "probably" does is almost always wrong, and each
wrong guess costs a round trip to disprove. One trace replaces a dozen guesses.

**What NOT to do** (each of these wastes hours):

- **Do not speculate a mechanism and then build on it.** "It's probably pacing / a look-ahead /
  an overrun" is a guess. Confirm it in a trace or throw it away. Do **not** propose a fix for a
  cause you have not observed.
- **Do not hand-decode bytes into instructions.** `DISASM` is the authoritative decoder — the
  same decode the CPU uses. Eyeballing opcodes invents instructions that are not there (and
  reading a PROM-shadowed region by hand yields garbage that looks like real code).
- **Do not add `printf`/file logging in the hot path.** It perturbs timing and hides the very
  timing bug you are chasing (a Heisenbug). The built-in recorder is passive — use it.
- **Do not fight the console with `expect`/pty prompt-matching.** Drive the monitor over `--mcp`
  (`monitor {command: …}`): one command in, clean text out, nothing to mis-sync.

**The method that works:**

1. **Reproduce deterministically** — the smallest input that shows the symptom, every time.
2. **Break on the exact event, not a guessed address.** The 6800 is **memory-mapped** — its I/O
   registers are addresses, not ports — so `BREAK MEM R <addr>` stops on a read of a device
   register (the 6850's data register at `$8005`, say), `BREAK MEM W <addr>` on a write, and
   `BREAK <addr>` (or `BREAK <addr> IF <expr>`) on code. A `MEM` break needs **no**
   reverse-engineering to place — you break on the access itself. To stop on the *byte* a load
   read rather than the fact of the read, use `BREAK MEM R <addr> LOADS <expr>` — it is judged
   after the instruction, so the register holds what just arrived (the status bit that finally
   came up, the byte that was out of range).
3. **Sweep, then read.** The `HISTORY` recorder is **always on**, so the moment a break fires the
   run-up to it is already captured — `HISTORY CPU 500` dumps the last 500 instructions with their
   registers, no arming needed. To go forward instead, `STEP 500` runs quietly and `HISTORY CPU
   500` dumps what it just ran. Either way, read what actually executed — do not summarize it in
   your head, read it.
4. **Follow the one datum.** Track the specific byte in `A`, the register, or the memory write
   through those instructions: where it is stored (`STAA <addr>`), where control branches, and who
   called the code (stack pointer depth and the return address). Run a **working** case beside a
   **failing** one and find the single instruction where they diverge.
5. **Only then design the fix** — against the confirmed cause, never the theory.

Worked example — the console dropping a character from a pasted command. `BREAK MEM R 8005`
(the 6850 console data register), paste the line, then `STEP`/`HISTORY` from each read and follow
the byte in `A`. The trace shows, as fact: every typed byte *is* read from the ACIA; which byte
reaches the command line and which is thrown away; and the exact instruction where a kept byte and
a dropped byte part company. "The console probably loses bytes somewhere" was a guess that led
nowhere for hours; the `HISTORY` dump answered it in minutes. Reach for the trace first.

### The debugger commands — reach for the one that fits

The whole debugger is reachable over MCP, nearly all of it through `monitor {command: …}`; only
`regs` and `mem_dump` have dedicated tools. You do not memorize these — `monitor {command: "HELP
<cmd>"}` prints any one's syntax — but you do need to know they *exist*, because the right command
turns a guess into a fact. Grouped by what you are trying to see:

**Where the processor is, and moving it forward**

| To… | Command | Why it is the one |
|---|---|---|
| See the CPU now | `regs` (or `REGS`) | Free on every stop — you rarely type it. The last column is the next instruction, already disassembled. |
| Run one instruction, or *n* | `STEP` / `STEP 20` | Real bus cycles through the real decode — it *is* the machine moved forward one instruction. Prints the registers after each. |
| Step **over** a `JSR`/`BSR` | `NEXT` (`N`) | Runs the callee at full speed and stops the instant it returns — so you stay in the code you are reading instead of touring a print routine. On anything else it is a single step. |
| Jam the PC and look | `EXAMINE <addr>` | Sets PC to `<addr>` (the front-panel switch), then shows the register line and the instruction `STEP` will run. |

**Stopping on the exact event**

| To… | Command | Why it is the one |
|---|---|---|
| Reach a code address | `BREAK <addr>` / `BREAK <lo>-<hi>` | PC lands **on** it, nothing there has run yet — `STEP` runs it fresh. |
| Catch whatever writes/reads memory or a device | `BREAK MEM W <addr>` / `BREAK MEM R <addr>` | Watches the **bus**, not an instruction — so on this memory-mapped machine it catches an access to a RAM location *or* a device register (a 6850 at `$8004`, the DC-4 at `$8018`) with no address to reverse-engineer. This is how you find who clobbers a byte. |
| Stop only in the case you care about | `BREAK <addr> IF <expr>` | Condition on the registers. **A bare word is that register; a literal needs a leading zero** — `0A` is ten, `A` is the accumulator. `== != < > <= >= && \|\| & \|` and parens. Works on `MEM` breaks too. |
| Stop on the byte a load **read** | `BREAK MEM R <addr> LOADS <expr>` | Judged *after* the instruction, so the register holds the byte that just arrived. `IF` sees the inputs, `LOADS` the result. |
| Stop when a cassette auto-stops | `BREAK TAPE STOP` | A device watch — halts inside the loader the moment the tape parks, without knowing the loader's end address. |
| List / clear | `BREAK` / `NOBREAK [id]` | Ids are plain decimals, not bus addresses. |

**Reading and changing memory**

| To… | Command | Why it is the one |
|---|---|---|
| Read a block | `mem_dump` / `DUMP <addr>` | Peeks — runs no bus cycle, consumes nothing. Hex plus ASCII; read a string straight out of the right column. |
| Disassemble | `DISASM <addr> <count>` | Peeks, and decodes for the 6800 in the machine. **Must start on an opcode** — one byte off and the listing is fiction (it re-syncs a line or two later, so it can look right while its first instruction is a phantom). Single-step to a known boundary if unsure. |
| Patch one byte / a run | `DEPOSIT <addr> <bytes>` / `EDIT <addr>` | A **real** bus write — it says so if nothing decodes the address, rather than pretending. `EDIT` also assembles an instruction in place. |
| Name things | `SYMBOLS LOAD prog.PRN` | Then `BREAK START`, and `DISASM` reads `JSR OUTCH`, not a bare address. A `.PRN`/`.LST` listing is richer than a `.SYM` (it marks `EQU`s). |
| Find / fill / move / compare | `SEARCH` `FILL` `MOVE` `COMPARE` | `COMPARE <range> <file>` checks what the machine loaded against what you meant to load. |

**The bus and the boards**

| To… | Command | Why it is the one |
|---|---|---|
| Poke a board like the guest would | `DEPOSIT <addr> <val>` / `DUMP <addr>` | **Real** bus cycles with every side effect — a write to `$8005` transmits a byte, a read consumes one, a read of the DC-4 status advances nothing but reports it. This machine is memory-mapped, so a device register is just an address. |
| Ask who answers | `WHO <addr>` | No cycle run. When a read gives you `FF`, `WHO` tells you whether a board answered with that byte or **the bus floated because nobody decodes it** — the single most useful disambiguation on this machine. Also flags contention. |
| See the whole backplane | `SHOW BUS MAP\|IRQ\|CONTENTION` | `IRQ` is the *only* window on interrupt wiring — a board strapped to a line nobody listens to fails in total silence. `CONTENTION` finds two boards on one address range in a machine you built yourself. |
| Hear a board narrate itself | `SHOW DEBUG`, then `SET <ch> DEBUG=<flag>` | Instrumented parts (`dc40` sector/seek, the `6850` serial, a `socket` connect) describe what they do in their own terms, each line prefixed with the PC that drove it. |

**The machine over time**

| To… | Command | Why it is the one |
|---|---|---|
| See what led to the stop | `HISTORY [n]` / `HISTORY BUS [n]` | A flight recorder that is **always on** — the run-up to any break is already recorded. Each `HISTORY` line reads like a `STEP`; `HISTORY BUS` is raw cycles naming *who drove* and *who answered* (a floated read shows `--`). |
| Trace a region as it runs | `TRACE ON [file] [MASK=IRQ,CONTENTION]` | Logs every matching cycle. A **tracepoint** — `BREAK <addr> TRACE ON` and `BREAK <addr> TRACE OFF` — flips tracing on entering a subroutine and off leaving it, without ever stopping the machine. |
| Copy the whole session | `SET CONSOLE log=session.txt` | Guest output and your input to a host file as they happen. |
| Save and return to a moment | `SNAPSHOT <file>` / `RESTORE <file>` | Saves *state*, not configuration — `RESTORE` reads it back into a machine of the same shape (build the shape first with a machine file or `CONFIG LOAD`). |

The full reference, with a worked session for each, is the **Debugger** document
(`swtpcsim-debugger.pdf`, shipped beside this file); every command's one-line syntax is in
`cheatsheet.md`.

### Drive it through a persistent `--mcp` session, not a pty

The debugging loop above only works if controlling the machine is *effortless* — one command in,
its answer out, decide the next. You get that by keeping **one `--mcp` process open** (the minimal
driver near the top of this guide) and sending one `tools/call` per step: `monitor {command: …}`
is literally "enter a monitor command, read its text, enter the next," with **no console echo and
no prompt to match**. `run`, `regs`, `send`, `recv` fill in the rest. That is the loop for
stepping and tracing.

**Do not reach for `expect` or a raw pty to drive the interactive monitor for this.** A pty echoes
your keystrokes back *interleaved* with the machine's output, and matching the `swtpcsim> ` prompt
races the **stale** prompt already sitting in the buffer — so your captures come back empty or as
fragments of the next command, and a `RUN` followed by typed input races the monitor against the
guest over who reads the line. If you catch yourself logging a whole session to a file to grep
afterward, you have already lost the loop: stop and drive it over `--mcp`.

This is also the only way to reliably send a real **carriage return** — and that is the single
biggest "the simulator is broken" false alarm, so it earns its own gotcha:

- **A `\r` reaches the guest as 0x0D only if your client JSON-*escapes* it.** The server feeds
  the bytes of the decoded `input` string verbatim, so `json.dumps({"input": "CAT\r"})` from a
  persistent Python session puts a genuine 0x0D on the wire. But some tool wrappers pass the two
  characters `\` and `r` literally, or send an Enter as LF (0x0A) — and **SWTBUG and FLEX read the
  console a character at a time and want a true CR** to end a line. When Enter seems ignored, stop
  guessing at the program — drive from the persistent Python `--mcp` session, where `\r` is a real
  0x0D.

Two more gotchas once you do:

- **`notifications/initialized` gets no reply.** After `initialize`, send it as a JSON-RPC
  *notification* (no `id`) and do **not** try to read a line back for it — waiting for a response
  that never comes hangs the driver. Then begin your `tools/call`s.
- **Confirm the guest is actually at its prompt before you type.** Under `--mcp` a machine's
  `startup` parks, and a boot idles through the monitor sign-on that `run`'s idle heuristic reads
  as "done." Loop `run` until the guest reaches its interactive prompt (`$`, `.`, or FLEX's `+++`)
  — press Enter with `run {input: "\r"}` and watch the prompt echo back — *before* you feed a
  command, or a boot-time reader swallows your first characters.

## Attaching a serial port to a board

A serial channel `CONNECT`s to an endpoint: `console | null | loopback | serial:/dev/tty… |
socket:PORT | socket:HOST:PORT`.

```
board_add {type: "680uio", id: "uio0"}                              # a 680b Universal I/O board
monitor  {command: "SET uio0:serial BAUD=9600"}                     # baud is a unit strap
monitor  {command: "CONNECT uio0:serial serial:/dev/tty.usbserial-XXXX"} # a real host port
monitor  {command: "CONNECT uio0:serial loopback"}                  # TX->RX plug, for self-test
```

The console board is the `mps` (SWTPC MP-S) on `swtpc` — one 6850 ACIA at the SS-30 slot base
(`$8004` control/status, `$8005` data), addressed `mps0:tty`. A second serial line comes from the
`680uio`'s 6850 (`uio0:serial`); its 6820 PIA sections are `uio0:p1a`/`uio0:p1b`. On `altair680`
the onboard console 6850 is `io0:tty`.

Framing (8N1 …) is **not** a setting — the guest writes it into the 6850 control register and a
real host port is reprogrammed to follow. During every `run`, each connected line is serviced,
so a program shuttling bytes between the console and a second port works live: bytes the far end
sends arrive on the guest console, and bytes typed on the console arrive at the far end.

### The 6850 ACIA register crib

Each 6850 fills its four-address SS-30 slot: only A0 reaches the register select, so
`base+0`/`base+2` are the **control**(write)/**status**(read) register and `base+1`/`base+3` are
the **data** register (the last two mirror the first two — SWTBUG's power-up probe depends on that
mirror). For the `mps` console at `$8004`: status/control at `$8004`, data at `$8005`.

- **Status (read), true sense:** `RDRF=0x01` (rx full), `TDRE=0x02` (tx empty), `DCD=0x04`,
  `CTS=0x08`, `IRQ=0x80`.
- **Control (write):** bits 0-1 divide (`11`=master reset), bits 2-4 word select, bits 5-6
  transmit/RTS control (`00`=RTS **low/asserted**+TIE off, `10`=RTS **high/deasserted**+TIE off,
  `11`=break), bit 7 = RIE.
- **Always two writes:** `0x03` (master reset — latches and *holds* the chip) then a real
  divide+word-select. 8N1 = **`0x15`** (÷16, RTS asserted, no interrupts); 8N2 = `0x11`. To
  drop RTS without disturbing framing, write `0x55`. Master reset does **not** clear the other
  control bits; a bus RESET does **not** touch the 6850 (it has no reset pin).

### Driving a real serial device — the clock is the trap

The moment the far end is real hardware with real reply latency, the CPU clock stops being a
performance knob and becomes a **timing** one. A period driver times a serial read with an
instruction-count busy-loop calibrated for one clock. `clock_hz` rescales that constant:
`SET cpu0 clock_hz=2000000` halves a 1 MHz timeout, `clock_hz=0` (flat-out, the default) burns it
in microseconds. Run the guest too fast and its timeout shrinks below the device's actual reply
latency — the read times out, retries, and desyncs **before the answer arrives**. Pin `clock_hz`
to the clock the guest's constants assume — **1 MHz for a classic SWTPC/680b** — unless you have
measured headroom.

How much margin you need is timeout-vs-latency, not the clock alone. A per-transaction protocol
(the device pauses to seek or process between requests) needs a big margin; a back-to-back
streaming transfer tolerates far less. And a serial poll loop is I/O-bound: past a modest clock,
raising `clock_hz` buys **no** throughput, it only erodes the margin. Don't reach for a faster
clock to "speed up" a wire-bound transfer.

**Watch the wire.** Tee a line to a chronological hex dump — the single best view of exact TX/RX
interleaving and timing:

```
monitor {command: "CONNECT uio0:serial serial:/dev/cu.usbserial-XXXX |cap.log?fmt=dump&ts=elapsed&gap=50"}
```

`gap` is a bare millisecond integer (`gap=50`, not `50ms`; `0` = never break a row on a pause).
The tap lives **in the sim process**, so it stops on `QUIT` — a late reply a device streams after
you have quit never reaches the log.

**One owner per host port.** Exactly one process may hold `/dev/cu.…`; close any `pyserial` (or
other swtpcsim) that has it, or the attach fails busy. swtpcsim already flushes stale RX on open
(`tcflush`), so an external flush is redundant — and worse, opening the port from pyserial toggles
DTR/RTS, which can knock a device out of its current mode. Let the sim own the port.

**`timeout_ms` bounds a live transfer too.** Traffic on a real device does not extend the budget.
Give a streaming read a `timeout_ms` as long as its worst case (up to
600000 ms) and let `until` end the call early, same as any other command; a call that hits
`timeout_ms` mid-transfer returns `stopped: "timeout"` with what it read so far, which is a normal
result to resume `run` (no new `from`) on, not a failure. Watch a destination pointer climb via
`regs`/`mem_dump` if you want to confirm it is still making progress rather than stuck.

## Toward a real machine

The reason the serial attach matters: the endpoint a channel `CONNECT`s to is the only thing that
changes between the simulator and the metal. Build and debug a program on the simulated machine —
where you can single-step it and dump its memory — and when it works, `CONNECT` the same channel
to `serial:/dev/cu.…` (a USB-to-serial cable to a real SWTPC 6800 or Altair 680b) and send the
identical bytes at real hardware. The guest program does not know the difference; only the
endpoint moved. That makes the simulator a bench for the real machine: prove it here, then run it
there, and when they disagree you have a known-good side to compare against.

(On macOS use the `/dev/cu.*` name, not `/dev/tty.*` — `cu` does not block waiting for carrier.)

## Gotchas

- **Boot from the RESET entry, and wait for the prompt** — the two most common "it's broken" false
  alarms, both above: pass `from:` the monitor's RESET entry under `--mcp`, and loop `run` to the
  prompt before you type.
- **A real CR** — send `\r` from a JSON-escaping client so it reaches the guest as 0x0D (see
  above).
- **The 6800 is memory-mapped.** There are no I/O ports — a device is an address. Break with
  `BREAK MEM R|W <addr>`, ask `WHO <addr>`, and poke with `DEPOSIT`/`DUMP` at the register's
  address.
- **A read that floats to `0xFF`** — `WHO <addr>` tells you whether a board answered `FF` or the
  bus floated because nothing decodes it.
- **Debug at runtime** — `mem_dump` the (possibly self-modified) code and `regs` mid-run.

## Without the MCP (CLI fallback)

If you cannot run the MCP server, the same machine answers the monitor:

```
swtpcsim examples/flex/flex2-40.toml -x 'BOARDS' -i     # run a command, then stay interactive
swtpcsim examples/flex/flex2-40.toml -s script.cmd      # run a command script, exit with status
```

But note: a bare monitor **`RUN` blocks on stdin under a pipe** (stdin is the script/JSON-RPC
channel), so anything that reads the guest console wants the MCP `run` tool or a real TTY /
`expect`. `STOP` = **`^E`** returns from a running guest to the monitor. There is no `BOOT` verb —
you start a machine by running from the monitor's RESET entry (`RUN E0D0` for SWTBUG), and FLEX is
booted from there with SWTBUG's `D` command.

## Where to go next

The **User Manual** (`swtpcsim-manual.pdf`, shipped beside this file) is the full reference —
the machines, the boards, the monitor, serial, disks, and the MCP server in depth. This guide
is the operator's crib for driving it all through MCP, and **`cheatsheet.md`** (also beside this
file) is the at-a-glance list of every option and command when you just need the syntax.
