#pragma once
#include <cstdio>
#include <string>

extern int g_fail;
extern int g_run;

#define CHECK(cond, what)                                                                \
    do {                                                                                 \
        ++g_run;                                                                         \
        if (!(cond)) {                                                                   \
            ++g_fail;                                                                    \
            std::printf("  FAIL  %s\n        at %s:%d\n", (what), __FILE__, __LINE__);   \
        }                                                                                \
    } while (0)

#define SECTION(name) std::printf("\n%s\n", name)

void test_clock();
void test_statefile();
void test_snapshot();
void test_bus();
void test_memory();
void test_readonly_props();
void test_paths();
void test_hex();
void test_srec();
void test_symbols();
void test_media();
void test_tnfs();
void test_cardimg();
void test_imd();
void test_tapecodec();
void test_roms();
void test_cli();
void test_console();
void test_debuglog();
void test_lineedit();
void test_tapecounter();
void test_idle_judgement();
void test_should_pace();
void test_achieved_hz();
void test_boundary();
void test_numbers();
void test_units();
void test_machines();
void test_load_is_atomic();
void test_clock_survives_load();
void test_subunit_schema();
void test_toml_notes();
void test_isa6800();
void test_asm6800();
void test_cpu6800();
void test_680board();
void test_680io();
void test_680uio();
void test_680kcacr();
void test_swtpc_mps();
void test_swtpc_dc4();
void test_lamp();
void test_expr();
void test_debug();
void test_lines();
void test_modemline();
void test_telnet();
void test_wd17xx();
void test_spindle();
void test_printer();
void test_tee();
void test_mirror();
void test_terminal();
void test_hostdir();
void test_mcp();
