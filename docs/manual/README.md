# swtpcsim — User Manual

A simulator of the **SWTPC 6800** and the **MITS Altair 680b**.

This is the manual for the program you have. It describes `swtpcsim` itself, the machines
built into it, and the worked examples that ship beside it. Media beyond those is
a separate thing, and the next chapter says where it will come from.

## Getting started

| | |
|---|---|
| [What swtpcsim is](introduction.md) | What it does, and what it does not do. |
| [What is in the package](package.md) | The binary, the built-in machines, the examples that boot — and where the rest is. |
| [Running it](running.md) | Unzip and go. |
| [**Quick start**](quick-start.md) | **Boot FLEX in one command.** Get out with `^E`, back in with `RUN`, out with `QUIT`. |

## Quick reference

| | |
|---|---|
| [Quick reference](ref/cheatsheet.md) | One page: the command line, every command, the machine-file skeleton, the boards. |

## Driving the machine

| | |
|---|---|
| [Machines](machines.md) | The command line, the built-in machines, and where a path is relative to. |
| [Machine files](configuring.md) | The TOML format, in full. |
| [Boards](boards.md) | What each board is, and what it is for. |

Driving `swtpcsim` *itself* — the `swtpcsim>` prompt and its debugger — is two separate
documents that ship beside this one. They are about the program, not the emulated hardware,
and each stands on its own:

| | |
|---|---|
| [The monitor](../monitor/monitor.md) — `swtpcsim-monitor.pdf` | The `swtpcsim>` prompt: prefix commands, the number rule, naming a board, STOP. |
| [Debugging](../debugger/debugging.md) — `swtpcsim-debugger.pdf` | Breakpoints, stepping, disassembly, and looking at the bus itself. |

## Using it

| | |
|---|---|
| [Disks](disks.md) | `MOUNT`, drive geometry, and getting back to the prompt. |
| [Tapes](tapes.md) | The cassette interface, and loading a program from tape. |
| [Serial, sockets and telnet](serial.md) | Wiring a board to your terminal, a TCP port, or a real UART. |
| [Worked examples](examples.md) | Complete sessions, start to finish. |
| [Driving it from an AI assistant](mcp.md) | The MCP server. |

## When it goes wrong

| | |
|---|---|
| [Troubleshooting](troubleshooting.md) | The things that catch everybody. |
| [Glossary](glossary.md) | SS-50, ACIA, FLEX, and the rest. |

## Reference

**Generated from the program itself** — every default, range and help string below is printed
from the same table the monitor resolves against, so it cannot disagree with what you have.

| | |
|---|---|
| [Boards and their parameters](ref/boards.md) | Every board, every key, every default. |
| [The built-in machines](ref/machines.md) | |

The command reference — every `swtpcsim>` command, with usage and examples — ships with
[the monitor document](../monitor/monitor.md) (`swtpcsim-monitor.pdf`), because that is what
it describes.

---

*Want to build a board of your own? That needs the source, and a different document — the
**Developer Guide**.*
