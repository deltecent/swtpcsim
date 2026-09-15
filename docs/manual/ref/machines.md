<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# The built-in machines

A built-in is a machine file that lives **inside the binary** — the same format you
would write yourself, with nothing special about it. Name one and you get it in any
directory on earth:

```
$ swtpcsim swtpc
```

`swtpcsim --list` prints this table, and `swtpcsim -x 'SHOW MACHINE' <name>` shows
what is actually in one.

| Machine | What it is |
|---|---|
| `altair680` | The Altair 680b -- MITS's second machine, and a different animal from the 8800. |
| `swtpc` | The SWTPC 6800 -- Southwest Technical Products' 1975 computer, the machine this simulator is named for. |

