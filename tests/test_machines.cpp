#include "test.h"

#include "boards/registry.h"
#include "config/toml.h"
#include "core/machines.h"

#include <string>
#include <vector>

using namespace swtpc;

// BUILT-IN MACHINES ARE IN THE BINARY, NOT ON THE DISK.
//
// This file never opens a file, and that is the entire point. swtpcsim ships as
// ONE EXECUTABLE plus documentation: it must not go looking for a machines/
// directory, an install prefix, or anything relative to argv[0]. If a built-in
// machine could only be found next to the binary, then copying the binary
// somewhere -- which is the ONLY thing anyone will do with it -- would break the
// machines, and it would break them at startup, which is the worst possible time.
// These tests pass with the source tree deleted.
//
// The other half of the deal: a built-in is REAL TOML, run through the SAME
// parser as your own config file. So if the config format changes under them,
// these go red instead of the machines quietly rotting into a second dialect.

void test_machines() {
    SECTION("built-in machines -- compiled in, not looked up");

    auto all = builtinMachines();
    CHECK(all.size() >= 2, "at least the two machines (altair680, swtpc) are compiled in");

    // EVERY BUILT-IN MUST ACTUALLY LOAD, and this loop is not ceremony -- it is here
    // because it WASN'T, and a machine shipped broken for exactly as long as it took
    // someone to run it by hand.
    //
    // A built-in that does not load is not a config bug. It is a BROKEN BINARY: these
    // are compiled in, so there is no file to fix.
    for (const auto& b : all) {
        CHECK(b.size > 0, "a built-in has bytes");
        CHECK(std::string(b.blurb).size() > 0, "and a blurb for --list");

        // AND THE BLURB IS A WHOLE SENTENCE, because it is ONE LINE and there is no
        // second one. cmake/embed_machines.cmake takes the FIRST comment line of the
        // .toml and nothing after it, so a description written across two lines does
        // not wrap in `--list` -- it is cut, and the half nobody sees is the half that
        // said what the machine is.
        //
        // A cut sentence has no full stop at the end of it. That is the whole test, and
        // it is cheap enough to run on every built-in forever.
        std::string blurb = b.blurb;
        CHECK(!blurb.empty() && blurb.back() == '.',
              "...and the blurb ENDS -- one line, one sentence, no second line to wrap to");

        Machine mm;
        std::string e;
        bool ok = loadMachine(b, mm, e);
        if (!ok) std::printf("        %s: %s\n", b.name, e.c_str());
        CHECK(ok, "...and it LOADS -- every built-in, not just one of them");
        if (!ok) continue;

        // ---- AND IT SURVIVES CONFIG SAVE. ----
        //
        // The writer is generic over properties() and unitProperties(); the reader has
        // to be generic over exactly the same pair, or CONFIG SAVE writes a file that
        // CONFIG LOAD refuses. Every built-in now round-trips, and the reload is compared
        // to the original rather than merely required not to error -- a loader that
        // silently dropped the units would pass the weaker test.
        std::string text = saveTomlText(mm);
        Machine     back;
        std::string e2;
        bool        reloaded = loadTomlText(text, std::string(b.name) + " (saved)", back, e2);
        if (!reloaded) std::printf("        %s: %s\n", b.name, e2.c_str());
        CHECK(reloaded, "...and CONFIG SAVE's own output loads straight back in");
        if (!reloaded) continue;

        CHECK(back.boards().size() == mm.boards().size(),
              "...with every board still in the backplane");
        CHECK(saveTomlText(back) == text,
              "...and saving it again is byte-identical: the round trip is a FIXED POINT, "
              "so nothing was silently dropped on the way through");
    }

    // ---- altair680: the 680b, MON680 in ROM, to the `.` prompt ----
    const BuiltinMachine* d = findMachine("altair680");
    CHECK(d != nullptr, "there IS an altair680 machine -- no config file required");
    if (!d) return;

    Machine m;
    std::string err;
    CHECK(loadMachine(*d, m, err), "and it loads through the ordinary TOML parser");
    CHECK(m.name == "altair680", "it knows its name");

    // A CPU, the onboard I/O and a memory card: three boards, and nothing else. The
    // 6800, the 680io console/strap board and the memory board (1K of RAM plus the
    // MON680 PROM) each arrived as ONE [[board]], with nothing else about the machine
    // moving.
    CHECK(m.boards().size() == 3, "a 6800 CPU, a 680io, and a memory card");
    CHECK(m.find("io0") != nullptr, "the onboard I/O -- the console 6850 and the strap port");
    CHECK(m.find("mem0") != nullptr, "the memory card RAM and the MON680 PROM live on");
    CHECK(m.cpu() != nullptr, "there is a processor in the 680b");
    CHECK(m.isa() == "6800", "and it speaks 6800, so DISASM never has to be told");
    CHECK(m.master() != nullptr, "and it can drive the bus");

    // The console MON680 prints its `.` on is in the backplane and connected.
    Board* io = m.find("io0");
    CHECK(io != nullptr, "and a 680io, because the monitor needs somewhere to talk");
    if (io) {
        UnitDef u;
        CHECK(io->findUnit("tty", u), "the serial unit exists");
        CHECK(u.state == "console", "and it is connected to the console");
    }

    // 1K of RAM at 0000-03FF, and NOTHING just above it. The 680b's onboard static RAM
    // is 1K placed at 0 by the jumpers; page 0 must be RAM because the monitor keeps its
    // stack and flags there.
    CHECK(!m.bus.lastUnclaimed(), "a board drives 0000");
    m.bus.memWrite(0x0000, 0x76);
    CHECK(m.bus.memRead(0x0000) == 0x76, "and it is RAM -- it stores a write");
    m.bus.memWrite(0x03FF, 0x3E);
    CHECK(m.bus.memRead(0x03FF) == 0x3E, "03FF is the last byte of the 1K");

    (void)m.bus.memRead(0x0400);
    CHECK(m.bus.lastUnclaimed(), "0400 is NOT populated -- nobody drives it");
    CHECK(m.bus.memRead(0x0400) == 0xFF, "so it floats to FF, which is the bus's answer");

    // ---- swtpc: the SWTPC 6800 with SWTBUG in ROM and the MP-S console ----
    const BuiltinMachine* k = findMachine("swtpc");
    CHECK(k != nullptr, "the SWTPC 6800 is here too");
    if (!k) return;

    Machine m4;
    CHECK(loadMachine(*k, m4, err), "swtpc loads");
    CHECK(m4.boards().size() == 4,
          "a 6800 CPU, an mps serial console, a dc4 floppy controller, and a memory card");
    CHECK(m4.cpu() != nullptr && m4.isa() == "6800", "it speaks 6800");
    m4.bus.memWrite(0x7FFF, 0x21);
    CHECK(m4.bus.memRead(0x7FFF) == 0x21, "7FFF is the top of the 32K RAM");

    // The MP-S console: the 6850 answers at 8004/8005 and mirrors up to 8006/8007
    // (only A0 reaches its register select -- the mirror SWTBUG's ACIA probe depends on).
    (void)m4.bus.memRead(0x8004);
    CHECK(!m4.bus.lastUnclaimed(), "8004 is the MP-S ACIA control/status -- a card answers");
    (void)m4.bus.memRead(0x8006);
    CHECK(!m4.bus.lastUnclaimed(), "and 8006 answers too -- it mirrors 8004");

    // The DC-4 floppy controller: the WD179x block at 8018 and the drive-select latch at 8014.
    (void)m4.bus.memRead(0x8018);
    CHECK(!m4.bus.lastUnclaimed(), "8018 is the DC-4 WD179x command/status -- a card answers");
    (void)m4.bus.memRead(0x8014);
    CHECK(!m4.bus.lastUnclaimed(), "8014 is the DC-4 drive-select latch -- a card answers");

    // SWTBUG at E000, and mirror-decoded to the top of memory so the reset vector at
    // FFFE/FFFF hands the 6800 SWTBUG's RESET entry (E0D0).
    CHECK(m4.bus.memRead(0xE000) == 0xFE, "E000: LDX (SWTBUG's first byte)");
    CHECK(m4.bus.memRead(0xFFFE) == 0xE0 && m4.bus.memRead(0xFFFF) == 0xD0,
          "the reset vector at FFFE/FFFF reads E0D0 through the FC00 mirror");
    CHECK(m4.bus.memRead(0xE3FE) == 0xE0 && m4.bus.memRead(0xE3FF) == 0xD0,
          "which is the same vector the ROM stores at E3FE/E3FF");

    // Case, and the absence of a machine, both answer honestly.
    CHECK(findMachine("ALTAIR680") != nullptr, "a machine name is not case-sensitive");
    CHECK(findMachine("swtpc") != nullptr, "the SWTPC is a built-in machine");
    CHECK(findMachine("imsai") == nullptr, "and one we have not built is simply not there");

    SECTION("the altair680's boot PROM -- the real MON680, on the real bus");

    // `swtpcsim altair680` then `D FF00` SHOWS YOU THE MONITOR PROM. That is the whole
    // point of a built-in: the machine you get for free is the one you wanted.
    //
    // The ROM is compiled in, decoded by the ordinary S-record loader, and put on the
    // bus by the ordinary memory board. Every one of those steps is tested in isolation
    // elsewhere; this checks they are actually WIRED TOGETHER, which is the one thing
    // unit tests routinely fail to notice has come apart.
    Machine md;
    CHECK(loadMachine(*d, md, err), "the altair680 machine loads");

    // MON680's first bytes, read through the bus, one byte at a time, as a CPU would:
    //     FF00  8D 22      BSR  ...        ; the monitor's entry preamble
    //     FF02  24 FC      BCC  ...
    //     FF04  C6 7F      LDAB #7F
    // (roms/MON680/MON680.ASM). This is the single 256-byte PROM at FF00-FFFF, PROM 1.
    CHECK(md.bus.memRead(0xFF00) == 0x8D, "FF00: BSR");
    CHECK(md.bus.memRead(0xFF01) == 0x22, "      ,22");
    CHECK(md.bus.memRead(0xFF02) == 0x24 && md.bus.memRead(0xFF03) == 0xFC, "FF02: BCC");
    CHECK(md.bus.memRead(0xFF04) == 0xC6 && md.bus.memRead(0xFF05) == 0x7F, "FF04: LDAB #7F");
    CHECK(!md.bus.lastUnclaimed(), "and the PROM really is driving the bus for all of it");

    // The RESET vector at the very top of memory is what the 6800 restarts from, and it
    // points INTO the PROM: FFFE/FFFF -> FFD8, the monitor's RESET entry (the reason
    // altair680.toml's startup is just RESET then RUN, with no address).
    CHECK(md.bus.memRead(0xFFFE) == 0xFF && md.bus.memRead(0xFFFF) == 0xD8,
          "FFFE/FFFF: the reset vector is FFD8, the monitor's own restart");

    // A GUEST CANNOT WRITE ROM. Not "the write is rejected" -- the board never answers
    // the cycle, so nobody drives it and the byte is simply gone.
    md.bus.memWrite(0xFF00, 0x00);
    CHECK(md.bus.lastUnclaimed(), "nobody decodes a write to the PROM");
    CHECK(md.bus.memRead(0xFF00) == 0x8D, "so the PROM is untouched");

    SECTION("a file or a built-in? decided by SPELLING, never by the filesystem");

    // If this were decided by probing the disk, `swtpcsim swtpc` would mean one thing
    // today and something else the day a file called `swtpc` appears in the working
    // directory. A command line whose meaning depends on its surroundings is a trap.
    CHECK(!looksLikeFile("swtpc"), "a bare word is a built-in name");
    CHECK(!looksLikeFile("altair680"), "even a longer one");
    CHECK(looksLikeFile("my.toml"), "a .toml is a file");
    CHECK(looksLikeFile("MY.TOML"), "in any case");
    CHECK(looksLikeFile("./swtpc"), "a path is a file, even with a built-in's name");
    CHECK(looksLikeFile("cfg/x"), "a slash anywhere makes it a file");
    CHECK(!looksLikeFile("toml"), "and 'toml' alone is not a .toml -- do not match a bare suffix");

    SECTION("a startup entry is a COMMAND LINE, so it can quote a filename");

    // A path with a space in it -- an S-record cassette, say -- needs the quotes around
    // it, and the monitor's tokenizer says so in as many words (cli/monitor.cpp: "a
    // filename is the one place a quote is not decoration but the only way to write a
    // path with a space in it").
    //
    // Which meant that until the escape below existed, THE ONE THING `startup` IS FOR
    // could not be written. Every `"` toggled the string, escape or not, so
    //
    //     startup = ["LOAD \"roms/demo tapes/hello.s19\""]
    //
    // parsed as `LOAD \` and the machine booted having loaded nothing -- no error, just
    // a file that was never read.
    const char* kQuoted = R"(
[machine]
name    = "quoted"
startup = [
  "LOAD \"roms/demo tapes/hello.s19\"",
  "EXAMINE 0100",
  "RUN 0100",
]
)";

    Machine     mq;
    std::string eq;
    CHECK(loadTomlText(kQuoted, "quoted", mq, eq), "a startup entry parses with escaped quotes");
    CHECK(mq.startup.size() == 3, "...all three of them, and the escape does not split one in two");
    if (mq.startup.size() == 3) {
        CHECK(mq.startup[0] == "LOAD \"roms/demo tapes/hello.s19\"",
              "...and the QUOTES REACH THE MONITOR, which is the whole point: without them "
              "the tokenizer sees three arguments and the path dies at the first space");
        CHECK(mq.startup[2] == "RUN 0100", "...and an entry with no escape in it is untouched");
    }

    // The round trip, which is where this broke the SAME way CONFIG SAVE broke on units:
    // the writer emitted the quotes raw, so the file it produced closed the TOML string
    // early and would not load back.
    std::string qtext = saveTomlText(mq);
    Machine     qback;
    std::string eq2;
    CHECK(loadTomlText(qtext, "quoted (saved)", qback, eq2),
          "...and CONFIG SAVE's own output loads back in -- the writer escapes what the "
          "reader unescapes, or it is not a round trip");
    CHECK(qback.startup == mq.startup, "...with every command byte-identical");

    // AN UNKNOWN ESCAPE IS AN ERROR, NOT A SHRUG. `\n` and `\t` mean nothing to a monitor
    // command, and silently dropping the backslash would turn a Windows path written with
    // single separators into a shorter, wrong path that fails somewhere else entirely.
    Machine     mbad;
    std::string ebad;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nstartup = [\"LOAD \\nope\"]\n", "bad", mbad, ebad),
          "an escape this parser does not know is refused");
    CHECK(ebad.find("\\n") != std::string::npos, "...and the message names the offender");

    SECTION("base = \"altair680\" -- start from a machine and say what is DIFFERENT");

    // THE MACHINE IS THE HARD PART TO GET RIGHT, AND IT IS THE PART NOBODY WANTS TO
    // RETYPE. An expansion of the 680b is that machine plus one board -- that is the
    // whole of it -- and before `base` existed it had to restate every card to say so.
    //
    // This is not a convenience. Hand-copying a backplane is how you end up with a machine
    // that boots into a terminal that is not there, because the one card you forgot to
    // copy was the console.

    const char* kDelta = R"(
[machine]
name = "delta"
base = "altair680"

[[board]]
id = "io0"
)";
    Machine     md2;
    std::string ed;
    CHECK(loadTomlText(kDelta, "delta", md2, ed), "a delta on the altair680 machine loads");
    CHECK(md2.name == "delta", "...and `name` beats the base's, whatever order the keys are in");
    CHECK(md2.boards().size() == 3, "...with every card the base brought still in the backplane");
    CHECK(md2.find("mem0") != nullptr, "...including the memory card you did not have to remember");

    // `id` WITH NO `type` IS "THE ONE ALREADY IN THE MACHINE". It must not fit a second
    // card, and it must not silently do nothing.
    Board* base_io = md2.find("io0");
    CHECK(base_io != nullptr && base_io->type() == "680io", "an [[board]] with no `type` found the base's card");

    // ...and it is an ERROR when there is nothing to find, rather than a quiet no-op --
    // which is the difference between a typo you fix now and a machine that is missing a
    // card you will look for later.
    Machine     mno;
    std::string eno;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nbase = \"altair680\"\n\n[[board]]\nid = \"nope0\"\n",
                        "x", mno, eno),
          "...and an id that is in no base is refused, not ignored");

    // TYPE + AN ID THE BASE BROUGHT = REPLACE THE CARD OUTRIGHT. This is what makes a
    // memory board re-fittable: regions are a LIST, so appending a 24K region to the
    // base's board would OVERLAP it, not replace it. Naming the type says "this is the
    // whole card now" -- and the base's mem0 carries the MON680 PROM as well as the RAM,
    // so replacing it takes BOTH away.
    const char* kReplace = R"(
[machine]
name = "small"
base = "altair680"

[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0000
  size = "24K"
)";
    Machine     ms;
    std::string es;
    CHECK(loadTomlText(kReplace, "small", ms, es), "re-fitting a card the base brought loads");
    CHECK(ms.boards().size() == 3, "...and REPLACES it -- there is not a second memory board");
    ms.bus.memWrite(0x5FFF, 0x42);
    CHECK(ms.bus.memRead(0x5FFF) == 0x42, "5FFF is the top of the new 24K");
    (void)ms.bus.memRead(0x6000);
    CHECK(ms.bus.lastUnclaimed(),
          "...and 6000 is EMPTY: the base's 1K region is gone, not overlapped by the new "
          "one. Two boards both answering an address is bus contention, and it is exactly "
          "what appending would have built");
    (void)ms.bus.memRead(0xFF00);
    CHECK(ms.bus.lastUnclaimed(),
          "...and the MON680 PROM went with the card it was on. Replace means replace: if "
          "you want the ROM back, re-state it");

    // REMOVE takes a card out of the slot.
    const char* kRemove = R"(
[machine]
name = "noio"
base = "altair680"

[[board]]
id     = "io0"
remove = true
)";
    Machine     mr;
    std::string er;
    CHECK(loadTomlText(kRemove, "noio", mr, er), "a delta can pull a card out");
    CHECK(mr.boards().size() == 2, "...and the backplane really is one card lighter");
    CHECK(mr.find("io0") == nullptr, "...the onboard I/O is gone");
    (void)mr.bus.memRead(0xF000);
    CHECK(mr.bus.lastUnclaimed(), "...and nobody answers F000 any more -- it FLOATS, as an "
                                  "empty slot does");

    // A DUPLICATE ID WITHIN ONE FILE IS STILL AN ERROR, and this is the check REPLACE was
    // deliberately scoped around. A second [[board]] with a copy-pasted id is a typo; the
    // same thing against a BASE is intent.
    Machine     mdup;
    std::string edup;
    CHECK(!loadTomlText("[machine]\nname = \"d\"\n\n[[board]]\ntype = \"memory\"\nid = \"mem0\"\n"
                        "\n[[board]]\ntype = \"memory\"\nid = \"mem0\"\n",
                        "dup", mdup, edup),
          "two [[board]] tables with one id, in one file, is still refused");

    // `remove` and `type` contradict each other, and saying both is not a preference.
    Machine     mc;
    std::string ec;
    CHECK(!loadTomlText("[machine]\nname = \"c\"\nbase = \"altair680\"\n\n[[board]]\ntype = \"680io\"\n"
                        "id = \"io0\"\nremove = true\n",
                        "c", mc, ec),
          "`remove` and `type` together are refused -- one takes the card out, the other "
          "fits a new one");

    // A BASE THAT DOES NOT EXIST IS AN ERROR, not an empty machine.
    Machine     mb;
    std::string eb;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nbase = \"imsai\"\n", "x", mb, eb),
          "a base we have no machine for is refused");

    // AND A SAVED DELTA IS A MACHINE, NOT A DELTA. CONFIG SAVE writes the backplane it can
    // see -- every card, base or not -- so the saved file has no `base` key and stands on
    // its own. That is the only honest thing it can do: the base may be a FILE, and a file
    // can change under you.
    std::string dtext = saveTomlText(md2);
    CHECK(dtext.find("base") == std::string::npos, "a saved machine does not refer to a base");
    Machine     dback;
    std::string ed2;
    CHECK(loadTomlText(dtext, "delta (saved)", dback, ed2), "...and it loads on its own");
    CHECK(dback.boards().size() == md2.boards().size(), "...with all its cards written out");
}

// ---------------------------------------------------------------------------
// A LOAD EITHER HAPPENS OR IT DOES NOT (config/toml.cpp, machine.h replaceWith).
//
// A machine file is a MACHINE -- the whole backplane, the thing CONFIG SAVE wrote down
// -- and not a list of amendments to whatever happens to be running. It used to be the
// second thing, and it cost two bugs that nothing here could see, because every test in
// this file loads into a fresh `Machine` and a fresh Machine cannot tell the two apart.
// These load into a machine that ALREADY HAS CARDS IN IT, which is the case the monitor
// has and the tests did not.
// ---------------------------------------------------------------------------
void test_load_is_atomic() {
    SECTION("a machine file is a MACHINE: it replaces, and it is all or nothing");

    // A machine with something in it, and something to lose.
    Machine     live;
    std::string e;
    CHECK(loadMachine(*findMachine("altair680"), live, e), "the altair680 machine loads");
    CHECK(live.boards().size() == 3, "...and has three cards to lose");
    const Board* wasMem = live.find("mem0");
    CHECK(wasMem != nullptr, "...one of which is the memory card");

    // ---- THE FAILED LOAD CHANGES NOTHING ----
    //
    // THIS is the bug, and it was the nasty one: the load ran until it hit the bad
    // card, and everything it had already fitted stayed. The command REPORTED AN ERROR
    // AND CHANGED YOUR MACHINE ANYWAY -- leaving a backplane that was neither the one
    // you had nor the one you asked for, which is the worst of the three outcomes.
    // `memGOOD` comes FIRST on purpose: a file that fails on its first line proves
    // nothing, because there was never anything to leave behind.
    const char* kHalfBad = R"(
[machine]
name = "halfbad"

[[board]]
type = "memory"
id   = "memGOOD"

  [[board.region]]
  type = "ram"
  at   = 4000
  size = "1K"

[[board]]
type = "nosuchcard"
id   = "bad"
)";
    std::string why;
    CHECK(!loadTomlText(kHalfBad, "halfbad", live, why), "a file naming a card that does "
                                                         "not exist is refused");
    CHECK(live.boards().size() == 3, "...and the machine still has its three cards");
    CHECK(live.find("memGOOD") == nullptr, "...WITHOUT the card the bad file had already "
                                           "fitted before it died -- the load left nothing "
                                           "behind");
    CHECK(live.name == "altair680", "...and it is still the machine it was, by name");
    CHECK(live.find("mem0") == wasMem, "...and the cards are the SAME cards, not rebuilt "
                                       "ones: nothing was torn down and put back");

    // ---- THE ROUND TRIP THE MANUAL PROMISES (docs/manual/configuring.md) ----
    //
    // `CONFIG SAVE mine.toml` then `CONFIG LOAD mine.toml` is a worked example we ship,
    // under the words "it round-trips: load what it wrote and you get the machine back".
    // It did not. Loading MERGED into the live backplane, so the file CONFIG SAVE had
    // just written died on the first card it named: `a board with id 'io0' already
    // exists`. The round-trip test above this one passed throughout, because it loads
    // into a fresh Machine -- so the promise was tested on the one road nobody takes.
    std::string saved = saveTomlText(live);
    CHECK(loadTomlText(saved, "mine.toml", live, why),
          "the machine's own CONFIG SAVE output loads back into the RUNNING machine -- "
          "the round trip the manual sells");
    CHECK(live.boards().size() == 3, "...and it is still three cards, not six");
    CHECK(saveTomlText(live) == saved, "...and it is the same machine: save it again and "
                                       "the text is identical");

    // ---- `base` NOW WORKS HERE AT ALL ----
    //
    // Not a bonus -- a thing that was DEAD. `base` refuses to run once the machine has
    // boards in it (it is what the boards are a change TO), so under the old merge every
    // file with a `base` was unloadable at the prompt, and said so with an error about
    // key order that had nothing to do with what was wrong.
    const char* kDerived = R"(
[machine]
name = "derived"
base = "altair680"

[[board]]
id   = "mem0"
fill = "zero"
)";
    CHECK(loadTomlText(kDerived, "derived", live, why),
          "a file with a `base` loads into a machine that already has cards");
    CHECK(live.name == "derived", "...and it is the derived machine now");
    CHECK(live.boards().size() == 3, "...with the base's cards, once each");
}

// ---------------------------------------------------------------------------
// A MOVED CARD BRINGS ITS CRYSTAL WITH IT (issue #34).
// ---------------------------------------------------------------------------
// The scratch machine above is what makes a load all-or-nothing, and it cost this:
// `clock_hz = 2000000` in a machine file set the property on the CPU card, the card
// announced it to the SCRATCH machine's Clock, and replaceWith then moved the card onto
// the real backplane -- whose Clock had never heard of it. `SHOW cpu0` read 2000000 off
// the card, the run loop free-ran, and every word of both was true.
//
// IT SURVIVED BECAUSE EVERY TEST SET THE CRYSTAL AT THE MONITOR. `SET cpu0 clock_hz=...`
// runs on a card that is already on the real backplane, so it was never the broken path
// -- and neither was `CONFIG LOAD`, which powers the machine again afterwards and
// republished by luck. The one road nothing drove was the one every operator takes:
// put it in the file, start the simulator. So this test loads it FROM A FILE and asks
// the CLOCK, not the card.
void test_clock_survives_load() {
    SECTION("clock_hz in a machine file reaches the CLOCK, not just the card (#34)");

    const char* kPaced = R"(
[machine]
name = "paced"
base = "altair680"

[[board]]
id       = "cpu0"
clock_hz = 2000000
idle     = false
)";
    Machine     m;
    std::string err;
    CHECK(loadTomlText(kPaced, "paced", m, err), "a machine file with a crystal loads");

    // THE CARD. This half was never broken, and on its own it is exactly the reassuring
    // half-truth that hid the bug for a day.
    Board* cpu = m.find("cpu0");
    CHECK(cpu != nullptr, "the CPU card is in the backplane");

    // THE CLOCK. This is the half the run loop actually reads.
    CHECK(!m.clock.free(), "the run loop PACES: a crystal in the file is a crystal in the clock");
    CHECK(m.clock.hz() == 2000000, "...at the rate the file asked for");
    CHECK(!m.clock.idle(), "and `idle` rides the same wire -- it was lost the same way");

    // AND THE DEFAULT IS STILL FLAT OUT. The fix republishes on every attach, so the
    // card that says nothing must go on saying nothing (clock.h: 0 is free-running, and
    // it is the default).
    Machine     f;
    std::string e2;
    CHECK(loadTomlText("[machine]\nname = \"flat\"\nbase = \"altair680\"\n", "flat", f, e2),
          "a machine file with no crystal loads");
    CHECK(f.clock.free(), "...and runs flat out, which is the default and stays the default");
}

// ---------------------------------------------------------------------------
// SUB-UNIT TABLES HAVE A SCHEMA, AND IT IS THE ONLY ONE.
//
// `readonly` was real, it worked, it was in no generated reference, no MCP schema and no
// SHOW -- because the keys of [[board.drive]] and [[board.region]] were known to nothing
// but a chain of string compares inside each board. That is a SECOND SCHEMA, and the
// project's central claim ("a board's properties ARE its TOML keys; no second schema
// anywhere") was false for the most user-facing TOML in the program.
//
// So: the keys are DECLARED (Board::subUnitProperties), the declaration is ENFORCED
// (Board::loadSubUnit, the one door), and these are the tests that keep it that way.
//
// NOTE: the floppy `drive` sub-unit table (its `readonly`/`media`/`writeprotect` keys)
// arrives with the DC-4 controller board in a later stage; those checks come back with
// it. For now the memory board's `region` table is the worked example, and the generic
// loop below is what protects a card that does not exist yet.
// ---------------------------------------------------------------------------
void test_subunit_schema() {
    SECTION("sub-unit tables: the keys are declared, and the declaration is the schema");

    // THE GENERIC ONE, AND THE ONLY ONE THAT PROTECTS A BOARD THAT DOES NOT EXIST YET.
    // A card that announces a table and declares no keys for it is the old bug, exactly:
    // it will load a machine file, refuse nothing, and document nothing. There is no way
    // to write that card and have this pass.
    for (const auto& t : boardTypes()) {
        auto b = makeBoard(t.name);
        for (const auto& table : b->subUnitTables()) {
            auto schema = b->subUnitProperties(table);
            // The message names the card, because a failure here is about ONE card and
            // the loop is over all of them.
            std::string why = "every [[board." + table + "]] on a " + t.name +
                              " declares its keys -- a table with no schema documents "
                              "nothing and validates nothing";
            CHECK(!schema.empty(), why.c_str());
            for (const auto& p : schema)
                CHECK(!p.name.empty() && !p.help.empty(),
                      "...and every key has a name and a line of help, because a generated "
                      "reference prints both");
        }
    }

    // THE MEMORY CARD, AND ITS `region` TABLE: the one sub-unit schema in the 6800 world
    // today, and enforced at the one door.
    std::string err;
    auto        m = makeBoard("memory");

    // The keys the machine files actually use are declared, with their kinds.
    auto region = m->subUnitProperties("region");
    bool haveType = false, haveAt = false, haveSize = false;
    for (const auto& p : region) {
        if (p.name == "type") {
            haveType = true;
            CHECK(p.kind == Kind::Enum && p.choices.size() == 2,
                  "`type` is an enum, ram|rom, off the card's own table -- not an if-chain");
        }
        if (p.name == "at")   haveAt = true;
        if (p.name == "size") haveSize = true;
    }
    CHECK(haveType, "the region kind is DECLARED -- this is the bug, in one check");
    CHECK(haveAt && haveSize, "and so are `at` and `size`");

    // ENFORCED, AND AT THE ONE DOOR. Each of these used to be a hand-written check in a
    // board (or, for some of them, no check at all).
    CHECK(!m->loadSubUnit("region", {{"type", "eprom"}, {"at", "0"}}, err),
          "`type = eprom` is refused -- ram|rom is a declared enum now, not an if-chain");
    CHECK(!m->loadSubUnit("region", {{"type", "ram"}, {"at", "0"}, {"sise", "48K"}}, err),
          "and a misspelled `size` is refused, not ignored");
    CHECK(err.find("size") != std::string::npos,
          "...and the refusal LISTS THE KEYS IT DOES TAKE, which is most of the cure");
    CHECK(m->loadSubUnit("region", {{"type", "ram"}, {"at", "0000"}, {"size", "48K"}}, err),
          "...while the real thing still loads: `at` is hex and `size` takes a K, off the "
          "property's own radix");

    // A TABLE THIS CARD DOES NOT HAVE IS REFUSED BY THE SAME DOOR.
    CHECK(!m->loadSubUnit("drive", {{"unit", "0"}}, err),
          "the memory card has no `drive` table, and the one door says so");
}

// ---------------------------------------------------------------------------
// `#>` NOTES ARE THE AUTHOR SPEAKING TO THE OPERATOR (config/toml.cpp, stripComment).
//
// An ordinary `#` comment is dropped, as it always was. A `#>` comment is captured and
// handed back so the load command can echo it -- boot instructions, "type CAT at the
// FLEX prompt", "disk B is blank". It is still a comment: it configures nothing and
// CONFIG SAVE does not write it back. These pin the capture rules that the display sites
// depend on.
// ---------------------------------------------------------------------------
void test_toml_notes() {
    SECTION("`#>` comment lines are captured for the operator, ordinary `#` are not");

    const char* kNoted =
        "# this is an ordinary comment and is DROPPED\n"
        "#> Boots MON680 to its `.` prompt.\n"
        "#> Type J FD00 to run the cassette loader.\n"
        "#>\n"
        "#> The demo tape is already in the recorder.\n"
        "[machine]\n"
        "name = \"noted\"\n"
        "base = \"altair680\"\n";

    Machine                  m;
    std::string              err;
    std::vector<std::string> notes;
    CHECK(loadTomlText(kNoted, "noted", m, err, &notes), "a file with `#>` notes loads");
    CHECK(notes.size() == 4, "...and every `#>` line is captured, in order");
    CHECK(notes[0] == "Boots MON680 to its `.` prompt.", "...the single leading space is eaten");
    CHECK(notes[1] == "Type J FD00 to run the cassette loader.", "...the second line is next, in order");
    CHECK(notes[2] == "", "...a bare `#>` is a blank line, not a dropped one");
    CHECK(notes[3] == "The demo tape is already in the recorder.", "...and the last line is last");

    // The `#` inside a QUOTED value is not a comment at all, so it is neither stripped
    // from the value nor mistaken for a note.
    const char* kHashInPath =
        "#> a real note\n"
        "[machine]\n"
        "name = \"hash\"\n"
        "base = \"altair680\"\n"
        "startup = [\"LOAD \\\"a#b.s19\\\"\"]\n";
    Machine                  mh;
    std::string              eh;
    std::vector<std::string> hnotes;
    CHECK(loadTomlText(kHashInPath, "hash", mh, eh, &hnotes), "a `#` inside a quoted path loads");
    CHECK(hnotes.size() == 1 && hnotes[0] == "a real note",
          "...the `#` in the path is NOT captured as a note");
    CHECK(mh.startup.size() == 1 && mh.startup[0].find("a#b.s19") != std::string::npos,
          "...and the `#` survives IN the value, uncut");

    // A trailing `#> note` after real code keeps the code and still captures the note.
    const char* kTrailing =
        "[machine]\n"
        "name = \"trail\"  #> named after nothing in particular\n"
        "base = \"altair680\"\n";
    Machine                  mt;
    std::string              et;
    std::vector<std::string> tnotes;
    CHECK(loadTomlText(kTrailing, "trail", mt, et, &tnotes), "a trailing `#>` on a key line loads");
    CHECK(mt.name == "trail", "...the key's value is intact, the comment did not bleed in");
    CHECK(tnotes.size() == 1 && tnotes[0] == "named after nothing in particular",
          "...and the trailing note is captured");

    // A `base` machine's OWN notes do not surface: notes belong to the file the operator
    // loaded, not to everything it inherits. `altair680` has no `#>` of its own, so a file
    // that inherits it and adds none of its own comes back with an empty list.
    Machine                  mb;
    std::string              eb;
    std::vector<std::string> bnotes;
    CHECK(loadTomlText("[machine]\nname = \"quiet\"\nbase = \"altair680\"\n", "quiet", mb, eb, &bnotes),
          "a file that inherits and writes no notes of its own loads");
    CHECK(bnotes.empty(), "...and surfaces no notes, its own or its base's");

    // Notes are display-only: CONFIG SAVE regenerates the machine and writes no `#>`, so
    // the round trip carries none -- exactly as it carries no ordinary comment.
    std::string saved = saveTomlText(m);
    CHECK(saved.find("#>") == std::string::npos, "CONFIG SAVE does not write `#>` notes back");
    Machine                  reload;
    std::string              er2;
    std::vector<std::string> rnotes;
    CHECK(loadTomlText(saved, "noted (saved)", reload, er2, &rnotes), "...and the saved file reloads");
    CHECK(rnotes.empty(), "...with no notes, because none were written");
}
