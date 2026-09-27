#include "test.h"

#include "boards/serial1602.h"   // Serial1602Board: the serial base under the 680 KCACR cassette
#include "boards/mits-680io.h"
#include "boards/mits-680uio.h"
#include "boards/swtpc-mps.h"
#include "boards/swtpc-mpt.h"
#include "boards/terminal-font.h"
#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/cardimg.h"
#include "host/media.h"
#include "host/terminal/stream.h"

#include <cstdio>

int g_fail = 0;
int g_run = 0;

namespace {

// The whole suite as an ordered name->function table, keyed by each test
// function's name minus its `test_` prefix. No args runs all of it in this
// order (identical to the historic flat call list, so the `unit` ctest test and
// CI are unaffected); named args run only the matching rows. See the plan and
// CLAUDE.md's Testing section -- this is a local iteration speed-up, not a
// substitute for the pre-commit full run.
const struct {
    const char* name;
    void (*fn)();
} kTests[] = {
    {"paths", test_paths},
    {"hex", test_hex},
    {"srec", test_srec},
    {"symbols", test_symbols},
    {"media", test_media},
    {"tnfs", test_tnfs},
    {"cardimg", test_cardimg},
    {"imd", test_imd},
    {"tapecodec", test_tapecodec},
    {"roms", test_roms},
    {"clock", test_clock},
    {"statefile", test_statefile},
    {"snapshot", test_snapshot},
    {"bus", test_bus},
    {"memory", test_memory},
    {"readonly_props", test_readonly_props},
    {"cli", test_cli},
    {"console", test_console},
    {"debuglog", test_debuglog},
    {"lineedit", test_lineedit},
    {"tapecounter", test_tapecounter},
    {"idle_judgement", test_idle_judgement},
    {"should_pace", test_should_pace},
    {"achieved_hz", test_achieved_hz},
    {"boundary", test_boundary},
    {"numbers", test_numbers},
    {"units", test_units},
    {"machines", test_machines},
    {"load_is_atomic", test_load_is_atomic},
    {"clock_survives_load", test_clock_survives_load},
    {"subunit_schema", test_subunit_schema},
    {"toml_notes", test_toml_notes},
    {"isa6800", test_isa6800},
    {"asm6800", test_asm6800},
    {"cpu6800", test_cpu6800},
    {"isa6809", test_isa6809},
    {"asm6809", test_asm6809},
    {"cpu6809", test_cpu6809},
    {"mp09", test_mp09},
    {"680board", test_680board},
    {"680io", test_680io},
    {"680uio", test_680uio},
    {"680kcacr", test_680kcacr},
    {"swtpc_mps", test_swtpc_mps},
    {"swtpc_mpt", test_swtpc_mpt},
    {"mc6840", test_mc6840},
    {"swtpc_dc4", test_swtpc_dc4},
    {"lamp", test_lamp},
    {"expr", test_expr},
    {"debug", test_debug},
    {"lines", test_lines},
    {"modemline", test_modemline},
    {"telnet", test_telnet},
    {"wd17xx", test_wd17xx},
    {"spindle", test_spindle},
    {"printer", test_printer},
    {"tee", test_tee},
    {"mirror", test_mirror},
    {"terminal", test_terminal},
    {"hostdir", test_hostdir},
    {"mcp", test_mcp},
};

bool nameEq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return false;
    }
    return *a == *b;
}

} // namespace

int main(int argc, char** argv) {
    // THE SAME WIRING main() DOES (DESIGN.md 7.7). The monitor knows the endpoint
    // grammar; boards do not. If the tests installed a DIFFERENT resolver -- a
    // convenient one that quietly turned `console` into a NullStream, say -- then
    // machines/default.toml would be exercised here in a configuration that no
    // user will ever run, and the first thing to break would be the real one.
    swtpc::Io680Board::setResolver(swtpc::resolveEndpoint);
    swtpc::Uio680Board::setResolver(swtpc::resolveEndpoint);
    swtpc::MpsBoard::setResolver(swtpc::resolveEndpoint);
    swtpc::Serial1602Board::setResolver(swtpc::resolveEndpoint);
    swtpc::MptBoard::setResolver(swtpc::resolveEndpoint);

    // The generic terminal endpoint reads these (issue #244). A NullDisplay is not
    // windowed, so `terminal:` refuses at CONNECT here -- which test_terminal asserts,
    // while it drives the VT100 engine directly (no window, no endpoint) to read the grid.
    static swtpc::NullDisplay g_display;
    swtpc::TerminalStream::setDisplay(&g_display);
    swtpc::TerminalStream::setFont(&swtpc::bundledTerminalFont());

    // The REAL media resolver, for the same reason -- and the SAME one the CLI
    // installs: openHostMedia routes an image with a `.geo` sidecar to a CardImage and a
    // plain file to a HostFile. A test that wants a disk without a filesystem installs a
    // MemoryMedia resolver for its length and puts this one back -- see test_media.cpp.
    swtpc::setMediaResolver(swtpc::openHostMedia);

    const int kCount = static_cast<int>(sizeof(kTests) / sizeof(kTests[0]));

    // Collect selectors, and handle --list up front.
    bool haveSelectors = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (nameEq(arg, "--list") || nameEq(arg, "-l")) {
            for (int t = 0; t < kCount; ++t)
                std::printf("%s\n", kTests[t].name);
            return 0;
        }
        haveSelectors = true;
    }

    if (!haveSelectors) {
        // No args: run the whole table in order, each under a header. The header is flushed
        // before the suite runs so a crash still names the suite it died in -- a SegFault
        // under ctest otherwise loses everything still sitting in the stdout buffer.
        for (int t = 0; t < kCount; ++t) {
            std::printf("== %s ==\n", kTests[t].name);
            std::fflush(stdout);
            kTests[t].fn();
        }
    } else {
        // Named selectors: an unknown name is a hard error, so a typo can't run
        // zero tests and print "0 failed" (which would read as a pass).
        for (int i = 1; i < argc; ++i) {
            bool matched = false;
            for (int t = 0; t < kCount; ++t) {
                if (nameEq(argv[i], kTests[t].name)) { matched = true; break; }
            }
            if (!matched) {
                std::fprintf(stderr,
                    "error: unknown test '%s' (try --list)\n", argv[i]);
                return 2;
            }
        }
        // Run matching rows in table order, each under a header.
        for (int t = 0; t < kCount; ++t) {
            bool selected = false;
            for (int i = 1; i < argc; ++i) {
                if (nameEq(argv[i], kTests[t].name)) { selected = true; break; }
            }
            if (!selected) continue;
            std::printf("== %s ==\n", kTests[t].name);
            std::fflush(stdout);
            kTests[t].fn();
        }
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
