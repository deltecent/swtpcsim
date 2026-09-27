cmake_minimum_required(VERSION 3.20)
project(swtpcsim VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# WERROR turns those warnings into build failures. OFF by default -- a casual build, or an
# unfamiliar toolchain whose newer diagnostics we have not yet chased down, must still compile.
# CI passes -DWERROR=on on every leg (.github/workflows/ci.yml), so a warning fails the build
# before merge; run `cmake -B build -DWERROR=on` locally to reproduce that gate before a PR.
#
# All three toolchains, now. GCC and Clang are clean at -Wall -Wextra -Wpedantic -Wshadow;
# MSVC is clean at /W4 once two things are handled (issue #238):
#
#   * Variable shadowing (MSVC C4456/C4457) is FIXED at the source -- the shadowing locals
#     were renamed. -Wshadow on GCC/Clang keeps the class from creeping back on every CI leg,
#     not just on Windows. The two do not flag the same set: GCC's -Wshadow reports a local
#     declared inside a lambda that hides a local of the enclosing function even when the
#     lambda does not capture it; Clang keeps that case in -Wshadow-uncaptured-local. Without
#     that flag a Mac build passes code that reds the Linux leg (issue #548), so Clang gets it
#     too. GCC rejects the flag as unknown, hence Clang only.
#   * Integer narrowing (MSVC C4244/C4267 -- uint32->uint8/16, size_t->uint32) is SUPPRESSED
#     tree-wide with /wd4244 /wd4267. It is the daily bread of an 8-bit emulator's byte/word
#     masking; GCC/Clang do not flag it at -Wall -Wextra either (only -Wconversion would), so
#     casting every truncation would be thousands of noise static_casts for no bug caught.
#
# With those settled, /WX joins the gate under WERROR on MSVC, matching -Werror on the others.
option(WERROR "Treat compiler warnings as errors; CI enforces this on all three toolchains." OFF)
if(MSVC)
  add_compile_options(/W4 /permissive- /wd4244 /wd4267)
  # The CRT "secure" deprecations (C4996 on getenv/fopen/strcpy/...) are MSVC nagging us to
  # use its non-portable _s variants. We use the standard C functions on purpose -- the code
  # is cross-platform -- so turn that family off. This is narrower than /wd4996: a genuine
  # [[deprecated]] use still warns.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  if(WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wshadow-uncaptured-local)
  endif()
  if(WERROR)
    add_compile_options(-Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# SANITIZE -- AddressSanitizer + UndefinedBehaviorSanitizer, in one flag.
#
# For memory and undefined-behaviour bugs that one platform's allocator hides and another's
# does not -- a use-after-free that is harmless on macOS can crash only on Windows. No
# compiler warning fires for those; a sanitizer names the exact file:line. Configure a side
# tree with it and the whole binary (library, tools, tests) is instrumented:
#
#   cmake -S . -B build-asan -DSANITIZE=on
#   cmake --build build-asan --target swtpc_tests
#   ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./build-asan/swtpc_tests
#
# Keep it in its OWN build directory -- an instrumented binary is slower and is not
# what you ship. It is deliberately NOT on by default and NOT wired into CI: CI catches
# a platform-only crash by running on that platform, and this is the tool you reach for
# once it does.
#
# GCC/Clang get both sanitizers. MSVC has had /fsanitize=address since VS2019 and it
# catches the same use-after-free class, so honour the flag there too -- it just has no
# UBSan half to add. (MSVC ASan wants the RELEASE CRT and its runtime DLL on PATH; a
# Debug config or a stale PATH is the usual "it built but won't launch".)
# ---------------------------------------------------------------------------
option(SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (dev only)" OFF)
if(SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    message(STATUS "SANITIZE=on -- AddressSanitizer (MSVC has no UBSan). "
                   "Use a Release config and keep the ASan runtime DLL on PATH.")
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "SANITIZE=on -- Address + UndefinedBehavior sanitizers active. "
                   "Run with ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Built-in ROMs (DESIGN.md 10.3.1)
#
# Each subdirectory of roms/ is one ROM. Its .HEX or .BIN is embedded VERBATIM
# as a byte array; the C++ side runs the ordinary Intel HEX loader over those
# bytes at startup. One parser, two sources (a host file, or .rodata) -- so a
# built-in cannot drift from a mounted file, and CMake never learns what Intel
# HEX is. Provenance and CRC32 for every ROM live in docs/roms.md.
# ---------------------------------------------------------------------------
# CONFIGURE_DEPENDS: re-glob on every BUILD, not just on configure. Without it,
# dropping a new ROM into roms/ does nothing until someone remembers to re-run
# cmake -- and DELETING one breaks the build outright, with an error naming a
# file you just deliberately removed. Both are ten confusing minutes, and both
# are avoidable for one word.
set(ROMS_GENERATED "${CMAKE_BINARY_DIR}/generated/roms_generated.cpp")
file(GLOB ROM_DIRS RELATIVE "${CMAKE_SOURCE_DIR}/roms"
     CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/roms/*")

set(ROM_IMAGE_FILES "")
foreach(d ${ROM_DIRS})
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/roms/${d}")
    file(GLOB img CONFIGURE_DEPENDS
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.HEX" "${CMAKE_SOURCE_DIR}/roms/${d}/*.hex"
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.BIN" "${CMAKE_SOURCE_DIR}/roms/${d}/*.bin"
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.S19" "${CMAKE_SOURCE_DIR}/roms/${d}/*.s19"
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.SREC" "${CMAKE_SOURCE_DIR}/roms/${d}/*.srec")
    list(APPEND ROM_IMAGE_FILES ${img})
    # The SHOW ROMS description is read from roms/<NAME>/DESC, so editing one must
    # re-run the embed -- same as changing the image itself.
    file(GLOB descsrc CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/roms/${d}/DESC")
    list(APPEND ROM_IMAGE_FILES ${descsrc})
  endif()
endforeach()

add_custom_command(
  OUTPUT "${ROMS_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DROMS_DIR=${CMAKE_SOURCE_DIR}/roms
          -DOUT=${ROMS_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake ${ROM_IMAGE_FILES}
  COMMENT "Embedding ROMs from roms/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Built-in machines (DESIGN.md 10.0)
#
# machines/*.toml are BUILD-TIME INPUTS, exactly like a .cpp file. They are read
# here, at compile time, and emitted as byte arrays that link into .rodata.
#
# THE SHIPPED BINARY IS SELF-CONTAINED. It never looks for a machines/ directory,
# never consults an install prefix, and does not care where it was copied to --
# `swtpcsim default` works from a USB stick. That is a hard requirement: this
# ships as ONE EXECUTABLE plus documentation, and a simulator that cannot find
# its own default machine is a simulator that does not start.
#
# The .toml files are still real TOML, in the real config format, because they
# are parsed at runtime by the same loadTomlText() that reads your config file.
# So they double as worked examples, and they cannot drift from the format.
# ---------------------------------------------------------------------------
set(MACHINES_GENERATED "${CMAKE_BINARY_DIR}/generated/machines_generated.cpp")
file(GLOB MACHINE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/machines/*.toml")

add_custom_command(
  OUTPUT "${MACHINES_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DMACHINES_DIR=${CMAKE_SOURCE_DIR}/machines
          -DOUT=${MACHINES_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_machines.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_machines.cmake ${MACHINE_FILES}
  COMMENT "Embedding machines from machines/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Which commit this binary is.
#
# Between releases, MOST binaries are untagged: every CI artifact, every local
# build, anything handed to somebody to try. `swtpcsim 0.1.0` names all of them
# identically, so a bug report against one cannot be traced to the source that
# produced it. `git describe` can, and the build is the only place that knows.
#
# THE FALLBACK IS "unknown", AND THAT IS DELIBERATE. A release tarball has no .git
# in it, and neither does a source zip; there is nothing to describe and nothing
# to infer from. Writing PROJECT_VERSION there instead would produce a string that
# looks like provenance and is not -- and the way you find out it lied is by
# spending an afternoon on a bug in a revision the binary was never built from.
#
# CONFIGURE TIME, not build time: git runs once and the answer is baked into one
# generated header, so every object file in the binary agrees about it and no
# compile shells out. `git status` is consulted too, since
# "which commit" is a half-answer for a tree with uncommitted edits in it -- which
# is exactly the tree most hand-built binaries come from. Both are a snapshot; the
# CONFIGURE_DEPENDS below re-runs this when HEAD moves, and a build that only
# edited files still reports the dirty flag it was configured with. Re-run cmake if
# you need it exact.
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(SWTPCSIM_GIT_COMMIT "unknown")
set(SWTPCSIM_GIT_DIRTY 0)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --always
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_rc)
  # A non-zero status means git could not answer -- no commits yet, or this is not a
  # work tree after all. Leave it "unknown"; a half-captured sha is worse than none.
  if(git_rc EQUAL 0 AND git_describe)
    set(SWTPCSIM_GIT_COMMIT "${git_describe}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE git_status
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE status_rc)
    # Untracked files are excluded on purpose: a build/ directory, an editor's swap
    # file and a scratch disk image are not modifications to the source this binary
    # was built from, and a dirty flag that is always set says nothing.
    if(status_rc EQUAL 0 AND git_status)
      set(SWTPCSIM_GIT_DIRTY 1)
    endif()
  endif()
  # So that a commit, a checkout or a rebase re-configures and the sha follows. HEAD
  # is the file that moves in all three; the ref it names moves when a commit lands on
  # the branch you are already on.
  #
  # ONLY FOR AN ORDINARY .git DIRECTORY. In a linked work tree `.git` is a FILE naming
  # the real gitdir elsewhere, so there is no .git/HEAD here to watch -- the version is
  # still captured correctly above (git itself follows the pointer), it just does not
  # re-configure by itself. Chasing that indirection would buy one convenience and a
  # second parser for git's on-disk layout.
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git" AND EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/.git/HEAD")
    file(READ "${CMAKE_SOURCE_DIR}/.git/HEAD" head_contents)
    if(head_contents MATCHES "^ref: ([^\r\n]+)")
      set(head_ref "${CMAKE_SOURCE_DIR}/.git/${CMAKE_MATCH_1}")
      if(EXISTS "${head_ref}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${head_ref}")
      endif()
    endif()
  endif()
endif()

configure_file("${CMAKE_SOURCE_DIR}/cmake/version.h.in"
               "${CMAKE_BINARY_DIR}/generated/version_generated.h" @ONLY)

# ---------------------------------------------------------------------------
# The platform layer (DESIGN.md 2.1). NO #ifdefs -- ANYWHERE.
#
# OS differences are one interface header with zero conditionals in it, plus one
# implementation file per OS, and CMAKE picks the directory. This block is the
# entire mechanism, and it is the only place in the build that asks what OS it is
# on. Nothing in src/ ever does.
#
# The win32 half is FIELD-PROVEN, not merely written: serial against two real FTDI
# ports, socket against the real TCP stack, terminal against a real console, each a
# leg of `ctest -L hw` (docs/porting-notes.md has the counts). This comment used to
# say it was written-but-unbuilt and cite that same file as agreeing; the file has
# said the opposite since 2026-07-15.
#
# MSVC is the ONLY supported Windows toolchain -- it is what CI builds and what is shipped.
# Nothing here is knowingly MSVC-only (no _MSC_VER anywhere in src/, lowercase Windows headers,
# plain main()); MSVC is simply the only toolchain tested and shipped.
# ---------------------------------------------------------------------------
if(WIN32)
  set(PLATFORM_SOURCES
    src/platform/win32/home_win32.cpp
    src/platform/win32/localtime_win32.cpp
    src/platform/win32/serial_win32.cpp
    src/platform/win32/socket_win32.cpp
    src/platform/win32/terminal_win32.cpp)
else()
  set(PLATFORM_SOURCES
    src/platform/posix/home_posix.cpp
    src/platform/posix/localtime_posix.cpp
    src/platform/posix/serial_posix.cpp
    src/platform/posix/socket_posix.cpp
    src/platform/posix/terminal_posix.cpp)
  # A baud with no standard termios constant -- 76800, an S-100 staple -- is set through
  # an OS-specific ioctl: BOTHER/termios2 on Linux, IOSSIOSPEED on macOS. A genuine
  # per-OS split is a per-OS FILE, never an #ifdef in the shared serial_posix.cpp
  # (DESIGN.md 2.1). CMake picks the one that matches.
  if(APPLE)
    list(APPEND PLATFORM_SOURCES src/platform/posix/macos/serial_custombaud_macos.cpp)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PLATFORM_SOURCES src/platform/posix/linux/serial_custombaud_linux.cpp)
  endif()
endif()

# ---------------------------------------------------------------------------
# ...AND THE LINT THAT KEEPS IT TRUE (DESIGN.md 2.1).
#
# The rule above is worth exactly as much as its enforcement. Without this, the first
# time somebody needs one small thing on Windows an #ifdef appears "just this once",
# and inside a month you are SIMH.
#
# It is a BUILD dependency of the library, not a test, because 2.1 says "fails the
# build" -- a rule you can merge and fix later is a rule you have already lost. It
# greps for OS macros AND for OS headers; cmake/lint_platform.cmake explains why the
# second half is not optional.
# ---------------------------------------------------------------------------
add_custom_target(platform_lint
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                           -P ${CMAKE_SOURCE_DIR}/cmake/lint_platform.cmake
  COMMENT "DESIGN.md 2.1: checking the OS has not escaped src/platform/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Core library -- everything except main(). Tests link this.
# ---------------------------------------------------------------------------
add_library(swtpc_core STATIC
  ${PLATFORM_SOURCES}
  src/core/version.cpp
  src/core/crc32.cpp
  src/core/statefile.cpp
  src/core/hex.cpp
  src/core/symbols.cpp
  src/core/paths.cpp
  src/core/bus.cpp
  src/core/clock.cpp
  src/core/board.cpp
  src/core/machine.cpp
  src/core/roms.cpp
  src/core/machines.cpp
  src/core/value.cpp
  src/core/expr.cpp
  src/core/debug.cpp
  src/core/debuglog.cpp
  src/core/spindle.cpp
  src/isa/isa.cpp
  src/isa/isa6800.cpp
  src/isa/isa6809.cpp
  src/cpu/cpu6800.cpp
  src/cpu/cpu6809.cpp
  src/host/stream.cpp
  src/host/filter.cpp
  src/host/console.cpp
  # The built-in terminal engine: an attributed grid (TerminalScreen), the painter that
  # renders it into a Display (TerminalRenderer), and the abstract dialect it drives
  # (TerminalEmulator, header-only). Lifted from the VDB-8024 board; the shared basis for
  # the generic terminal endpoint (issue #244). See src/host/terminal/.
  src/host/terminal/screen.cpp
  src/host/terminal/renderer.cpp
  # The generic terminal endpoint (issue #244): the VT100/ANSI dialect, the name->factory
  # table of emulations, and TerminalStream -- the engine wearing a ByteStream face, so
  # `connect = "terminal"` gives every serial board a windowed terminal for free.
  src/host/terminal/vt100.cpp
  src/host/terminal/adm3a.cpp
  src/host/terminal/vt52.cpp
  src/host/terminal/h19.cpp
  src/host/terminal/emulations.cpp
  src/host/terminal/stream.cpp
  src/host/endpoint.cpp
  src/host/hostserial.cpp
  src/host/tcp.cpp
  # `ModemLine` -- a software-controlled phone line: an inbound connection is a RING
  # held silent until the guest answers, an outbound dial happens on off-hook, and an
  # idle modem holds no sockets. The PMMI MM-103 (Phase 2) drives it; unlike tcp.cpp's
  # auto-answer server it never picks up on its own.
  src/host/modemline.cpp
  # `file:PATH` -- a host file as a write-only sink: a printout, or a capture of a
  # line. A SINK, so it is trivial next to hostserial/tcp (no pump, no read path).
  src/host/file.cpp
  # `printer:QUEUE` -- a host print queue as a write-only sink that buffers bytes into
  # JOBS (host/printer_stream.h, issue #70). Always compiled and pure: it knows how to
  # decide where one job ends but nothing about CUPS -- the platform submit backend is
  # linked separately and only when found (the printer feature block below).
  src/host/printer_stream.cpp
  # `ENDPOINT|FILE` -- a transparent hex TAP on any line (host/tee_stream.h). A
  # decorator ByteStream: it copies bytes past in both directions and forwards the rest,
  # observe-only and 8-bit clean, so it is exempt from the "one FilterStream" rule.
  # The Telnet NVT protocol (RFC 854) as a pure byte engine, no sockets -- shared by
  # the telnet: endpoint and the PMMI modem's answer/dial line (host/telnet_codec.h).
  src/host/telnet_codec.cpp
  # `telnet:PORT` / `telnet:HOST:PORT` -- socket: plus the Telnet protocol handshake
  # (host/telnet_stream.h). A decorator over TcpStream: on a new session it offers
  # WILL ECHO / SGA so a stock `telnet` client drops its local echo and goes
  # character-at-a-time, and it strips inbound IAC. socket: stays a raw pipe.
  src/host/telnet_stream.cpp
  src/host/tee_stream.cpp
  # `ENDPOINT|socket:PORT` -- a live BIDIRECTIONAL mirror of a line over TCP: a human
  # telnets in, watches the session, and can type back onto the wire (issue #381). The
  # tee's twin (same decorator shape) but the sink is a socket, not a file, and it is
  # two-way. The watcher never paces the guest (host/mirror_stream.h).
  src/host/mirror_stream.cpp
  src/host/media.cpp

  # A MediaFile that lives on a TNFS server (FujiNet's network filesystem), so a disk
  # or tape image mounts straight off the network: MOUNT ... tnfs://host/image.dsk. It
  # slots in behind openHostMedia like any other medium (src/host/tnfs.h).
  src/host/tnfs.cpp
  # A host DIRECTORY as one CF/microSD card: a `card.geometry` descriptor maps LBA runs
  # to per-volume backing files, streamed lazily (seek per block, never slurped like a
  # HostFile). openHostMedia() is the resolver both mains install (src/host/cardimg.h).
  src/host/cardimg.cpp
  src/host/disk.cpp
  # ImageDisk (.IMD) -> raw sibling .DSK, at the MOUNT layer. Pure (bytes in, bytes out),
  # so DiskImage stays raw-only and the container never reaches it (src/host/imd.h).
  src/host/imd.cpp
  src/host/tape.cpp
  # The cassette modem and the container it arrives in. Neither knows what a board is:
  # a modulation is four numbers and a flag (host/tapemodem.h), so the 88-ACR and the
  # Sol-20 differ here as VALUES and not as code. Listed by the layering -- a WAV is a
  # container, an FSK signal is a modulation, a program image is neither.
  src/host/wav.cpp
  src/host/tapemodem.cpp
  # ...and the seam that joins them to a mount. A decorator over MediaFile, so a WAV
  # decodes ONCE at MOUNT and everything above it -- TapeImage, TapeStream, the UART --
  # sees the bytes a .TAP would have given it (host/tapecodec.h).
  src/host/tapecodec.cpp
  # The sandboxed host directory the Host Bridge reaches through. It is here, and not
  # on the board, because the board must never see a path, a symlink or a file handle
  # -- and because the escape tests that matter are tests of THIS, with no card in
  # sight (DESIGN.md 7, src/host/hostdir.h).
  src/host/hostdir.cpp
  # The chips. A chip is not a card: it is modeled from its DATA SHEET, it knows
  # nothing about S-100, and it is shared by every card that has one soldered to it.
  src/chips/mc6850.cpp
  # The 6820/6821 PIA -- Motorola's parallel-interface part. The 88-4PIO models it
  # inline; this shared register model is what the Altair 680b UI/O reuses.
  src/chips/mc6820.cpp
  # The TMS 5501 -- TI's NMOS I/O controller (UART + five timers + an eight-source
  # interrupt controller). Phase 1 models the UART half only; the Cromemco 4FDC/16FDC/
  # 64FDC each carry one as a console channel, and the TU-ART carries two. See
  # src/chips/tms5501.h.
  src/chips/sio2port.cpp
  src/chips/uart1602.cpp
  # The Intel 8251 USART -- the console chip of the SD Systems SBC-100/200, and the
  # next 8251 card to turn up gets it for free (src/chips/intel8251.h).
  src/chips/wd17xx.cpp
  src/boards/s100-memory.cpp
  # The generic Z80 CPU card -- the 88-CPU's twin (mits-z80cpu.h), carrying a
  # CpuZ80 instead of a Cpu8080. Listed after it so the reader sees the pair.
  # The Altair 680b CPU card -- the 88-CPU's twin one core down (mits-680cpu.h),
  # carrying a Cpu6800. Memory-mapped I/O, so it still decodes nothing.
  src/boards/mits-680cpu.cpp
  src/boards/cpu6809card.cpp
  src/boards/swtpc-mp09.cpp
  src/boards/mits-680io.cpp
  # The 680b Universal I/O expansion board -- a second 6850 serial + a 6820 PIA
  # (chips/mc6820.h), memory-mapped and S9-relocatable.
  src/boards/mits-680uio.cpp
  # The 680b KCACR audio-cassette interface -- an 88-ACR (AcrBoard) grown a motor and
  # moved to a memory-mapped, active-low register pair at F010/F011.
  src/boards/mits-680kcacr.cpp
  # The SWTPC MP-S serial console -- the 680io's twin on the SS-30 bus: a single 6850
  # at a configurable slot base (default $8004/$8005), the board SWTBUG talks to.
  src/boards/swtpc-mps.cpp
  src/boards/swtpc-dc4.cpp
  # The two MITS hard-sector floppy controllers, and the register model they share.
  # HardSectorFdc is not a convenience: MITS built the 88-MDS to present the 88-DCDD's
  # programming model, and the period BOOT.ASM declares the ports and every status/command
  # bit OUTSIDE its `if MINIDSK` blocks to prove it. What the two cards do NOT share --
  # 360 vs 300 RPM, 32 vs 64 us a byte, a head solenoid vs a motor that stops -- is what
  # each .cpp below actually contains. See src/boards/mits-hardsector.h.
  # The 88-HDSK Datakeeper hard disk -- a "controller does the work" card (Board
  # directly, not HardSectorFdc): it decodes A0h-A7h and serves 256-byte sectors from
  # a linear .DSK through a command/handshake protocol. See src/boards/mits-88hdsk.h.
  # The S100Computers Dual SD card -- another "controller does the work" card (Board directly):
  # two programmed-I/O ports (80/81) shift whole 512-byte sectors to/from a raw MediaFile card
  # (a DirectoryMedia directory card). No boot PROM. See src/boards/dualsd.h.
  # The S100Computers IDE-AB (CF) card -- the IDE/CompactFlash half of the IDE+ESP32 combination
  # board. An 8255 PPI (five ports at 30-34) drives a minimal ATA register file; whole 512-byte
  # LBA sectors move to/from the SAME raw MediaFile card dualsd uses. No boot PROM. See
  # src/boards/dualide.h.
  # The CompuPro System Support 1 -- a multifunction card. Being brought up in phases
  # (issue #392); today the OKI MSM5832 real-time clock. See src/boards/compupro-ss1.h.
  # The iCOM FD3712/FD3812 8" floppy -- like the 88-HDSK, a "controller does the work" card
  # (Board directly): two programmed-I/O ports (C0/C1) shift whole sectors, and a boot PROM
  # plus 6810 scratch RAM live in high memory. One superset engine serves both densities.
  # See src/boards/icom-fd3712.h.
  # Serial1602Board -- the reusable serial engine around a 1602-family (COM2502) UART:
  # the UART, the interrupt-enable flip-flops, and the card's own clock. A BASE, not a
  # registered board; the 680b cassette chain builds on it. See src/boards/serial1602.h.
  src/boards/serial1602.cpp
  # SD Systems VersaFloppy I/II -- a WD FD177x soft-sector floppy controller. Owns a
  # wd17xx chip and a generic DiskImageDrive adapter (floppy-drive). Boots SDOS.
  # SSM PB1 -- a 2708/2716 EPROM programmer + on-board EPROM board. A hybrid decode: a 4K
  # programming-socket window + one arm/type control port, plus a read-only on-board PROM area.
  # Tarbell #1011 (SD) / #2022 (DD) -- one card carrying a WD FD177x floppy controller
  # AND a 32-byte boot PROM that shadows low RAM over PHANTOM*. See src/boards/tarbell.h.
  # Cromemco 4FDC/16FDC/64FDC family -- one card carrying a WD FD177x floppy controller, a
  # TMS 5501 console UART, and an RDOS boot PROM at C000. A base (CromemcoFdcBoard) plus the
  # 16FDC/64FDC leaves (header-only); boots CDOS. See src/boards/cromemco-fdc.h.
  src/boards/floppy-drive.cpp
  # Cassette1602Board -- the reusable audio-cassette engine (FSK modem + tape transport:
  # MOUNT / WIND / REWIND / WAV codec). Derives from Serial1602Board; a BASE, not a
  # registered board -- the 680b KCACR builds on it. See src/boards/cassette1602.h.
  src/boards/cassette1602.cpp
  # The 88-C700 line-printer controller -- an output-only card that sinks its data
  # port to a ByteStream (a file, the console, a socket). No chip: a printer sends
  # nothing back, so there is no receiver to model. See mits-88c700.h.
  # The 88-LPC line-printer controller -- the C700's sibling for the 88-LP printer,
  # but line-buffered: 6-bit codes decoded to glyphs, committed as a line on PRINT or
  # at 80 chars. Sinks to a ByteStream like the C700. See mits-88lpc.h.
  # The 88-PIO parallel port: a bidirectional latch pair (out/in), each a ByteStream.
  # The 88-4PIO: up to four 6820 PIAs with software-set direction. See mits-88pio.h.
  # The Processor Technology VDM-1: memory-mapped 16x64 video. It renders into a
  # host Display (src/host/display.h) injected at the composition root -- an
  # SdlDisplay in the shipping binary, a NullDisplay headless -- so the card itself
  # links with no graphics library and is tested with no window. See proctech-vdm1.h.
  # The Cromemco Dazzler: color graphics scanned from a framebuffer in main RAM.
  # Renders into the same host Display seam as the VDM-1; no bus-mastering. See
  # cromemco-dazzler.h.
  # The SD Systems VDB-8024: an 80x24 video terminal on one board (two I/O ports, NOT
  # memory-mapped), the video console alternative to the SBC's 8251. Renders the same
  # host Display seam as the VDM-1 and takes its keyboard from a ByteStream like the
  # Sol. See sd-vdb8024.h.
  # The Processor Technology Sol-PC's integrated I/O as one composite card (serial,
  # keyboard, parallel, CUTS tape) -- reuses the vdm1 card for video. See proctech-sol.h.
  # OUR OWN CARD -- the only one in this list that is not a period board, and the
  # first genuinely new hardware built against the board API (DESIGN.md 12.1).
  src/boards/registry.cpp
  src/config/toml.cpp
  src/cli/monitor.cpp
  src/cli/commands.cpp
  src/cli/lineedit.cpp
  src/util/json.cpp
  src/mcp/server.cpp
  "${ROMS_GENERATED}"
  "${MACHINES_GENERATED}")

target_include_directories(swtpc_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# The optional video backend: SDL3 (DESIGN.md 7.4). THE FIRST OPTIONAL LINK-TIME
# DEPENDENCY, and it is DETECTED, not required.
#
# swtpcsim's loudest claim is that it builds with a C++20 compiler and CMake and
# nothing else -- so a graphics library cannot be a hard dependency. This block
# auto-detects an installed SDL3: found, and the video boards get a real window;
# absent, and they build headless against a NullDisplay (src/host/display_null.h)
# and every test still passes. Either way the build configures and succeeds. See
# docs/devguide/ for how to install SDL3.
#
# SWTPCSIM_ENABLE_SDL is ON by default (= "use SDL3 if it is here"); set it OFF to
# force a headless build even where SDL3 is installed. That is what a macOS UNIVERSAL
# build needs *against a Homebrew SDL3*, which is single-arch (arm64) and will not link
# into a fat binary. It is NOT a limit of SDL3: upstream's SDL3.xcframework carries a
# macos-arm64_x86_64 slice that is genuinely universal, so a universal windowed build is
# possible -- link that rather than brew's dylib.
#
# EXACTLY ONE CI LEG HAS SDL3: macOS, which installs it from Homebrew and builds native
# arm64. It is therefore the only place display_sdl.cpp is compiled, and the workflow
# FAILS that leg if it comes up headless -- otherwise the coverage lapses in silence,
# which is what happened before. Linux and Windows still take the not-found path.
#
# (Through v0.2.0 no leg had SDL3 at all, so nothing compiled display_sdl.cpp and every
# shipped binary was headless. This comment also used to claim the macOS leg passed
# -DSWTPCSIM_ENABLE_SDL=OFF; it never did.)
#
# ONLY src/host/display_sdl.cpp includes SDL. The boards see host/display.h and
# never learn a window exists, so they compile in every configuration; only this
# one translation unit and the composition root (src/main.cpp) are macro-gated.
# SDL is cross-platform, not an OS header, so it does not trip the 2.1 platform_lint.
# ---------------------------------------------------------------------------
option(SWTPCSIM_ENABLE_SDL "Build the SDL3-backed video boards if SDL3 is found" ON)
if(SWTPCSIM_ENABLE_SDL)
  find_package(SDL3 CONFIG QUIET)
  if(SDL3_FOUND)
    target_sources(swtpc_core PRIVATE src/host/display_sdl.cpp)
    target_link_libraries(swtpc_core PUBLIC SDL3::SDL3)
    target_compile_definitions(swtpc_core PUBLIC SWTPCSIM_ENABLE_SDL)

    # Whether the process may be brought to the foreground, and when (2.1: one file per
    # OS, CMake picks; src/platform/foreground.h says why it is needed at all). It is
    # selected HERE, inside the SDL block, rather than alongside the other platform
    # sources, because only a video board can raise the question -- so a headless build
    # never compiles it and macOS never links AppKit for a simulator with no window.
    if(APPLE)
      enable_language(OBJCXX)
      target_sources(swtpc_core PRIVATE src/platform/posix/macos/foreground_macos.mm)
      target_link_libraries(swtpc_core PUBLIC "-framework AppKit")
    elseif(WIN32)
      target_sources(swtpc_core PRIVATE src/platform/win32/foreground_win32.cpp)
    else()
      target_sources(swtpc_core PRIVATE src/platform/posix/foreground_posix.cpp)
    endif()

    message(STATUS "SDL3 found -- video boards enabled (windowed)")
  else()
    message(STATUS "SDL3 not found -- video boards build headless (null display). "
                   "Install SDL3 (see docs/devguide) for a window.")
  endif()
endif()

# ---------------------------------------------------------------------------
# The optional host-printer backend (docs/printing.md; issue #70). THE SECOND
# optional link-time dependency, and it follows the SDL3 rule above exactly: ON by
# default meaning "use it if it is here", a quiet detect, a message either way, and a
# build without it where `printer:` is simply not in the endpoint grammar and the
# resolver says so by name -- the one endpoint that cannot work on a given host fails
# at CONNECT with a sentence, not at compile.
#
# host/printer_stream.cpp is NOT here: it is pure (it decides job boundaries, it does
# not talk to a spooler) and is always compiled, so its boundary policy is tested with
# a fake sink on every platform, CUPS or no CUPS. Only the platform submit TU and the
# SWTPCSIM_ENABLE_PRINTER define that lights up the resolver arm are gated.
#
# macOS AND Linux are the SAME leg: both sit on CUPS, so one file (printer_cups.cpp)
# serves both and is selected with find_package(Cups) -- CMake's own module, which
# gives the imported Cups::Cups target. NOT pkg_check_modules: pkg-config is not
# guaranteed on a macOS dev box (this one has cups-config, not pkg-config), and a
# REQUIRED pkg check there would fail to configure a build that should just quietly
# skip the feature. Windows is WinSpool and is a later branch (issue #70 Phase 3).
option(SWTPCSIM_ENABLE_PRINTER "Print to a host queue if CUPS/WinSpool is found" ON)
if(SWTPCSIM_ENABLE_PRINTER)
  if(WIN32)
    # Phase 3 -- the file does not exist yet, so the feature is off on Windows until
    # it lands. Left here as the shape it will take (WinSpool, StartDocPrinter RAW).
    message(STATUS "printer: endpoint not yet built on Windows (issue #70 Phase 3)")
  else()
    find_package(Cups QUIET)
    if(Cups_FOUND)
      target_sources(swtpc_core PRIVATE src/platform/posix/printer_cups.cpp)
      target_link_libraries(swtpc_core PUBLIC Cups::Cups)
      target_compile_definitions(swtpc_core PUBLIC SWTPCSIM_ENABLE_PRINTER)
      message(STATUS "CUPS found -- printer: endpoint enabled")
    else()
      message(STATUS "CUPS not found -- printer: endpoint disabled. "
                     "Install libcups2-dev (Linux) for host printing.")
    endif()
  endif()
endif()

# Nothing links until the OS is where it belongs.
add_dependencies(swtpc_core platform_lint)

# The win32 socket layer (src/platform/win32/socket_win32.cpp) pulls in Winsock;
# link it. POSIX needs no equivalent -- sockets live in libc. This if(WIN32) is a
# build-script OS conditional, which is fine: the 2.1 lint governs C++ source, not
# CMake. PUBLIC so the test/tool executables that link swtpc_core inherit it.
if(WIN32)
  target_link_libraries(swtpc_core PUBLIC ws2_32)
endif()

add_executable(swtpcsim src/main.cpp)
target_link_libraries(swtpcsim PRIVATE swtpc_core)

# ---------------------------------------------------------------------------
# The manual's reference chapters, printed from the binary's own reflection layer.
#
# It links swtpc_core rather than shelling out to `swtpcsim` because no monitor command
# emits what a parameter table needs -- BOARDS TYPES has the help and not the defaults,
# SHOW has the defaults and not the help. See tools/gen-reference.cpp for the whole
# argument. It is a TOOL, not a feature: the shipping command surface is a contract, and
# a doc generator has no business in it.
#
# `docs-reference` rewrites docs/manual/ref/*.md and docs/monitor/ref/commands.md in the
# source tree, on purpose -- those files are committed, so that a reader of the repo (and the
# PDF build) needs no toolchain to see them. `docs-reference-check` below is what stops them
# going stale.
# ---------------------------------------------------------------------------
add_executable(swtpc_genref tools/gen-reference.cpp)
target_link_libraries(swtpc_genref PRIVATE swtpc_core)

# tapetool -- cassette audio in and out, with no machine attached. Same justification
# as swtpc_genref above: it is the tool you need BEFORE the simulator works, when the
# question is "what is actually on this tape?". See tools/tapetool.cpp.
add_executable(swtpc_tapetool tools/tapetool.cpp)
target_link_libraries(swtpc_tapetool PRIVATE swtpc_core)

add_custom_target(docs-reference
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/docs/manual/ref"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/docs/monitor/ref"
  COMMAND swtpc_genref "${CMAKE_SOURCE_DIR}/docs/manual/ref" "${CMAKE_SOURCE_DIR}/docs/monitor/ref"
  COMMENT "Generating docs/manual/ref/ and docs/monitor/ref/ from the binary")

# ---------------------------------------------------------------------------
# Tests -- no framework; a test is an executable that returns non-zero.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(swtpc_tests
  tests/main.cpp
  tests/test_680board.cpp
  tests/test_680io.cpp
  tests/test_680kcacr.cpp
  tests/test_680uio.cpp
  tests/test_swtpc_mps.cpp
  tests/test_swtpc_dc4.cpp
  tests/test_lamp.cpp
  tests/test_boundary.cpp
  tests/test_bus.cpp
  tests/test_cardimg.cpp
  tests/test_cli.cpp
  tests/test_clock.cpp
  tests/test_console.cpp
  tests/test_cpu6800.cpp
  tests/test_cpu6809.cpp
  tests/test_debug.cpp
  tests/test_debuglog.cpp
  tests/test_expr.cpp
  tests/test_hex.cpp
  tests/test_hostdir.cpp
  tests/test_imd.cpp
  tests/test_isa6800.cpp
  tests/test_isa6809.cpp
  tests/test_lineedit.cpp
  tests/test_lines.cpp
  tests/test_machines.cpp
  tests/test_mcp.cpp
  tests/test_media.cpp
  tests/test_memory.cpp
  tests/test_mirror.cpp
  tests/test_mp09.cpp
  tests/test_modemline.cpp
  tests/test_numbers.cpp
  tests/test_paths.cpp
  tests/test_printer.cpp
  tests/test_roms.cpp
  tests/test_snapshot.cpp
  tests/test_spindle.cpp
  tests/test_srec.cpp
  tests/test_statefile.cpp
  tests/test_symbols.cpp
  tests/test_tapecodec.cpp
  tests/test_tapecounter.cpp
  tests/test_tee.cpp
  tests/test_telnet.cpp
  tests/test_terminal.cpp
  tests/test_tnfs.cpp
  tests/test_units.cpp
  tests/test_wd17xx.cpp)
target_link_libraries(swtpc_tests PRIVATE swtpc_core)
# A couple of sections mount a real committed disk image (the SDOS video boot in
# test_vdb8024.cpp); `ctest` runs `unit` from the build tree, so give it the source root
# to resolve those paths.
target_compile_definitions(swtpc_tests PRIVATE SWTPC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME unit COMMAND swtpc_tests)

# ---------------------------------------------------------------------------
# The SOCKET half of the same idea, against the real kernel TCP stack. The `unit`
# aggregate's test_lines.cpp already proves the loopback happy path; this proves the
# NON-BLOCKING CONNECT, and above all the REFUSED connect that surfaces in select()'s
# except set -- the one Winsock behaviour docs/porting-notes.md flagged as most likely
# wrong. It uses only platform/socket.h, so it needs no hardware and no opt-in: a free
# loopback port is always there. Labelled `hw` because, like serialtest, it touches the
# real world (the kernel) rather than a ScriptedStream -- so `ctest -L hw` runs the
# whole real-world leg at once.
# ---------------------------------------------------------------------------
add_executable(swtpc_sockettest tests/sockettest.cpp)
target_link_libraries(swtpc_sockettest PRIVATE swtpc_core)
add_test(NAME socket-hw COMMAND swtpc_sockettest)
set_tests_properties(socket-hw PROPERTIES LABELS hw)

# ---------------------------------------------------------------------------
# The TERMINAL half, on a real console. The pipe path is proven in CI already
# (acceptance-basic4k/8k drive readInput's pipe branch and its broken-pipe EOF with a
# live guest); this proves the CONSOLE path a piped test can never reach -- SetConsoleMode
# raw mode, ENABLE_VIRTUAL_TERMINAL_INPUT, and the PeekConsoleInput peek-and-discard loop
# that keeps readInput non-blocking (porting-notes' #1 most-likely-wrong item).
#
# It lives in src/platform/win32/ and NOT in tests/ on purpose: proving that loop with no
# human means MANUFACTURING console input (WriteConsoleInput), which is a Win32 call, and
# DESIGN.md 2.1 keeps OS calls in the platform layer. A test of OS-specific code is
# OS-specific code -- so it sits beside the file it tests, is WIN32-only, and SKIPs (77)
# where no console can be taken. The POSIX terminal has its own mirror of it, below.
# ---------------------------------------------------------------------------
if(WIN32)
  add_executable(swtpc_terminaltest src/platform/win32/terminaltest_win32.cpp)
  target_link_libraries(swtpc_terminaltest PRIVATE swtpc_core)
  add_test(NAME terminal-hw COMMAND swtpc_terminaltest)
  set_tests_properties(terminal-hw PROPERTIES LABELS hw SKIP_RETURN_CODE 77)
else()
  # The POSIX mirror, and it is not optional any more. "Proven by hand against a pty"
  # is what let issue #25 through: an empty read on a VMIN=0 tty returns 0, the same
  # as a hung-up pipe, and reporting both as the end of input latched Console::eof_ on
  # every interactive run. It failed for a user in a terminal and passed here and in
  # CI, every time, because a piped stdin never takes that branch. This one opens a
  # pty and takes it. It SKIPs (77) where no pty can be had.
  add_executable(swtpc_terminaltest src/platform/posix/terminaltest_posix.cpp)
  target_link_libraries(swtpc_terminaltest PRIVATE swtpc_core)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(swtpc_terminaltest PRIVATE util)  # openpty
  endif()
  add_test(NAME terminal-hw COMMAND swtpc_terminaltest)
  set_tests_properties(terminal-hw PROPERTIES LABELS hw SKIP_RETURN_CODE 77)
endif()

# ---------------------------------------------------------------------------
# THE ACCEPTANCE TESTS -- period software, on the whole machine, through the CLI.
#
# A board test proves a board against its manual. This proves the MACHINE against an
# artifact somebody shipped in the 1970s, running unmodified: `swtpcsim swtpc` booting
# FLEX 2.0 off a real DC-4 floppy image, or MON680 signing on to the Altair 680b. It is
# the only test that can fail for a reason no unit test can see -- a board that is right
# in isolation and wrong in a backplane.
#
# It drives the REAL binary, so it also covers the monitor: MOUNT, LOAD, RUN, and a
# quoted path with spaces in it (which every period artifact has).
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# WHICH MACHINE DID I GET? ./swtpcsim.toml, found in the working directory.
#
# The one file the simulator FINDS rather than is GIVEN -- and the only thing keeping
# that from being the trap looksLikeFile() refuses to walk into is that it happens ONLY
# on a command line that names nothing. So most of this test is the negative controls:
# `swtpcsim swtpc`, `-m default` and `-n` must all be stone deaf to the directory
# they run in. If they ever are not, `swtpcsim swtpc` has started meaning different
# things in different places, which is precisely the 2am bug the CLI is arranged to
# prevent -- reintroduced by the back door, by a convenience.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-cwdconfig
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:swtpcsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cwdconfig.cmake)
set_tests_properties(acceptance-cwdconfig PROPERTIES LABELS acceptance TIMEOUT 60)

# The per-directory command-history file is written only for a human at a terminal; a
# script (-x/-s), a pipe, or --mcp must leave the directory untouched. This proves that
# negative -- the whole of the guarantee (see the file's own header for the pty/pipe trap).
add_test(NAME acceptance-history
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:swtpcsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/history.cmake)
set_tests_properties(acceptance-history PROPERTIES LABELS acceptance TIMEOUT 60)

# Under --mcp, stdout is the JSON-RPC transport: a machine file's `#>` notes must go to
# stderr, not ahead of the `initialize` reply where a strict client chokes on them (#459).
add_test(NAME acceptance-mcp-stdout
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:swtpcsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/mcp-stdout.cmake)
set_tests_properties(acceptance-mcp-stdout PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# SNAPSHOT / RESTORE through the real monitor -- the command wiring (DESIGN.md 13).
# The state round-trip is proved at the unit level (tests/test_snapshot.cpp); this
# proves the two commands write and read a file the operator named, and refuse one
# that is not a snapshot.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-snapshot
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:swtpcsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/snapshot.cmake)
set_tests_properties(acceptance-snapshot PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# THE MANUAL'S REFERENCE MAY NOT DRIFT FROM THE BINARY.
#
# docs/manual/ref/*.md is committed (a reader of the repo, and the PDF build, should need
# no toolchain to see it) and generated (a hand-typed parameter table is a second schema,
# and the first draft of one in this very project got three of the memory card's eight
# defaults wrong). Committed + generated = it can rot, so it is tested.
# ---------------------------------------------------------------------------
add_test(NAME docs-reference
         COMMAND ${CMAKE_COMMAND}
                 -DGEN=$<TARGET_FILE:swtpc_genref>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/docs-reference.cmake)
set_tests_properties(docs-reference PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# THE USER MANUAL MAY NOT NAME ANYTHING THE READER DOES NOT HAVE.
#
# It ships in the zip, next to the binary and the disks. There is no src/, no CMakeLists and
# no DESIGN.md in there. "Self-contained" erodes one helpful cross-reference at a time, in
# good faith, and nobody notices until a user follows one -- so it is a test, not a habit.
# The Developer Guide is deliberately exempt: it is FOR the source.
# ---------------------------------------------------------------------------
add_test(NAME docs-manual
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/docs-manual.cmake)
set_tests_properties(docs-manual PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# THE PACKAGE MAP AND THE MANUAL MUST NAME THE SAME PACKAGE.
#
# docs-manual (above) is a blacklist: it proves the manual names nothing OUTSIDE the package,
# but never reads docs/package.map. This is the other half -- the cheap one: a text comparison
# proving docs/package.map (the producer's manifest) and docs/manual/package.md (the reader's
# manifest) ship the same set. It caught the manual promising examples/debugger while the map
# did not. The EXPENSIVE half -- assemble the zip and run the manual's commands against it --
# still needs build-package.sh under a workflow and is deliberately not here.
# ---------------------------------------------------------------------------
add_test(NAME docs-package
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/docs-package.cmake)
set_tests_properties(docs-package PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# EVERY reference/ DISTILLATION MUST BE CATALOGUED.
#
# reference/*.md are the text-only hardware references. reference/README.md indexes them and
# docs/sources.md records the period scan each was distilled from -- both hand-maintained, and
# both went stale silently (four un-indexed files at once, caught only by a hand-run diff).
# This globs the distillations and fails if any is missing from either catalogue. Text only.
# ---------------------------------------------------------------------------
add_test(NAME reference-index
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/reference-index.cmake)
set_tests_properties(reference-index PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# The PDFs. NOT part of the build, and it must never become part of it.
#
# swtpcsim's loudest claim is that it builds with a C++20 compiler and CMake and nothing
# else. A PDF wants pandoc and a browser, and neither is allowed anywhere near the thing that
# produces the simulator. So this is a target you ASK for, and its absence is not an error --
# `cmake -S . -B build` on a machine with no pandoc must still configure and still build.
#
# YOU DO NOT HAVE TO RUN THIS ANY MORE, and you do not have to remember to commit what it
# makes: .github/workflows/docs.yml rebuilds both PDFs on every push to master that touches
# docs/, and commits them if their text actually moved. Nobody remembering was exactly the
# problem -- the shipped developer guide spent two days insisting the project had no CI.
#
# This target is still worth having: it is how you SEE a change before you push it, and the
# output is the same as CI's, because the fonts travel inside the document (docs/fonts/) and
# CI pins the same pandoc. Committing its output is fine and so is not committing it -- if
# your PDF matches, CI keeps it; if it does not, CI replaces it. What you must not do is
# hand-edit a PDF, or expect the committed one to stay as you built it.
# ---------------------------------------------------------------------------
add_custom_target(docs
  COMMAND "${CMAKE_SOURCE_DIR}/tools/build-docs.sh"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building swtpcsim-manual.pdf and swtpcsim-devguide.pdf"
  VERBATIM)
add_dependencies(docs docs-reference)

# ---------------------------------------------------------------------------
# THE INTERACTIVE ONE. Everything above drives the CLI through a PIPE, and a pipe is not
# a terminal -- so three things that only exist on a tty go untested by all of them: the
# echoed prompt and line editing, ATTN (^E) taking the console back from a running guest,
# and the run loop's decision to pace against a wall clock. This test types.
#
# It needs `expect`, which ships with macOS and is a package everywhere else. If it is not
# there we do not register the test rather than fail the suite over a missing tool -- and
# we SAY SO at configure time, because a test that silently is not there is worse than one
# that fails.
# ---------------------------------------------------------------------------
find_program(EXPECT_EXECUTABLE expect)
if(EXPECT_EXECUTABLE)
  add_test(NAME acceptance-cli
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cli.exp
                   $<TARGET_FILE:swtpcsim>)
  set_tests_properties(acceptance-cli PROPERTIES LABELS acceptance TIMEOUT 60)

  # ---------------------------------------------------------------------------
  # THE ALTAIR 680b: MON680 signing on to a terminal and answering. This is the 6800
  # line's integration oracle -- the disassembler, assembler, core, S-record loader, and
  # the `680io` memory-mapped console board all have to be right for the monitor to come
  # up and echo an examine of its own PROM. It TYPES, so it is an expect test for the same
  # receive-register race the eberhard header records.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-altair680
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/altair680.exp
                   $<TARGET_FILE:swtpcsim>)
  set_tests_properties(acceptance-altair680 PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # THE 680b KCACR: load a cassette S-record tape through the FD00 loader PROM.
  #
  # The companion to acceptance-altair680, one board up: it boots the same MON680, then
  # runs the KCACR loader (`J FD00`) to read a cassette into memory and reads the byte
  # back. It exercises the `680kcacr` board's active-low F010/F011 decode, the tape
  # transport underneath it, and the S-record loader PROM together -- if any is wrong,
  # the deposit never lands at 0200. It TYPES, so it is an expect test for the same
  # receive-register race. The tape (examples/altair680/kcacr-demo.tap) is in the tree.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-altair680-kcacr
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/altair680-kcacr.exp
                   $<TARGET_FILE:swtpcsim>)
  set_tests_properties(acceptance-altair680-kcacr PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # FLEX 2.0 off the DC-4: the SWTPC 6800 disk stack, end to end.
  #
  # The `swtpc` machine's integration oracle. SWTBUG's `D` disk-boot, the DC-4 drive-select
  # latch, the WD179x read path, the sector-0 boot coercion, and the FLEX filesystem all have
  # to be right for FLEX to load, reach its `+++` prompt, and CAT the directory off the disk.
  # It TYPES at a polled console, so it is an expect test (a pipe overruns the 1-byte ACIA --
  # see flex.exp). The FLEX 2.0 master (examples/flex/FLEX2-40.DSK) is in the tree,
  # mounted write-protected and shasummed before and after.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-flex
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/flex.exp
                   $<TARGET_FILE:swtpcsim>)
  set_tests_properties(acceptance-flex PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # FLEX9 off the DC-4 on the SWTPC 6809: the 6809's boot gate (DESIGN.md 3.2).
  #
  # The `swtpc09` machine's integration oracle. The 6809 core, the MP-09's DAT (loaded by
  # S-BUG from its power-up junk), S-BUG's `U` boot, the I/O window at E000, the WD179x read
  # path and the FLEX filesystem all have to be right for FLEX9 to reach `+++` and CAT the
  # disk. The FLEX9 2.8:3 disks (examples/flex9/) are in the tree, mounted
  # write-protected and shasummed before and after. All three shipped geometries boot, each
  # from its own example machine file: every disk in examples/flex9/ is booted by a test.
  # ---------------------------------------------------------------------------
  foreach(geom 35 40 DS)
    add_test(NAME acceptance-flex9-${geom}
             COMMAND ${EXPECT_EXECUTABLE}
                     -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/flex9.exp
                     $<TARGET_FILE:swtpcsim>
                     examples/flex9/flex9-${geom}.toml
                     examples/flex9/FLEX9-2.83-${geom}.DSK)
    set_tests_properties(acceptance-flex9-${geom} PROPERTIES
                         LABELS acceptance TIMEOUT 120
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()

else()
  message(STATUS "expect not found -- SKIPPING every interactive (pty-driven) acceptance "
                 "test, not just acceptance-cli: none of the tests that type at a guest are "
                 "registered without expect (as on Windows). The tty and mid-run paths "
                 "(prompt echo, ATTN, clock pacing, typed guest commands) go uncovered here.")
endif()
