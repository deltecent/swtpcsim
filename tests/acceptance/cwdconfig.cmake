# THE MACHINE NOBODY NAMED: ./swtpcsim.toml, found in the working directory.
#
# This is the ONE file the simulator finds rather than is given, and every other rule in
# the CLI exists to stop exactly that (looksLikeFile(), src/core/machines.h: a command
# line that changes meaning because of its surroundings is a trap). It is allowed here
# for one reason and one reason only: a BARE `swtpcsim` names no machine at all, so
# there is no meaning for the directory to change. That is the whole of the argument, and
# it collapses the moment the discovery leaks into a command line that DOES name one.
#
# So the negative cases below are not padding. They are the test.
#
# Expects: -DSIM=<swtpcsim> -DBIN=<binary dir>
#
# BIN IS PASSED IN, and this file climbs out of its own directory to find NOTHING. It used
# to say `${CMAKE_CURRENT_LIST_DIR}/../../build`, which is two lies in one line: that the
# tree above it is ours to walk, and that the build directory is called `build` (configure
# into `out/` and the test scribbles somewhere that does not exist). CMake knows where it
# put things -- ask it, in CMakeLists.txt, and pass the answer. Same bargain as -DSIM.
set(work "${BIN}/cwdconfig-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/proj" "${work}/empty")

# A machine that could not be mistaken for a built-in -- and a `base`, because the other
# half of this feature is that a found file is an ORDINARY config file: it can start from
# a built-in and say what is different, with no new machinery (docs/config.md).
file(WRITE "${work}/proj/swtpcsim.toml"
     "[machine]\nname = \"cwd-discovered\"\nbase = \"swtpc\"\n")

# `SHOW MACHINE` prints `name  <the machine>`, which is the only thing any of this is
# about: WHICH MACHINE DID I GET?
function(machine_in dir out_var err_var)
  execute_process(
    COMMAND           "${SIM}" ${ARGN} -x "SHOW MACHINE"
    WORKING_DIRECTORY "${dir}"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    err
    TIMEOUT           30
  )
  set(${out_var} "${out}" PARENT_SCOPE)
  set(${err_var} "${err}" PARENT_SCOPE)
endfunction()

function(expect_machine out want why)
  if(NOT out MATCHES "name[ \t]+${want}")
    message(FATAL_ERROR "cwdconfig: ${why}\n"
                        "  expected the machine to be '${want}'.\n--- output ---\n${out}")
  endif()
endfunction()

# ---- 1. A BARE COMMAND LINE, in a directory that has one. This is the feature. ----
machine_in("${work}/proj" out err)
expect_machine("${out}" "cwd-discovered" "a bare `swtpcsim` did not pick up ./swtpcsim.toml")

# ...AND IT SAID SO. A machine you did not ask for and were not told about is the
# twenty-minutes-lost bug this whole design is arranged to avoid, so the notice is part
# of the feature and not decoration.
if(NOT err MATCHES "no machine named")
  message(FATAL_ERROR
    "cwdconfig: ./swtpcsim.toml was used SILENTLY -- no notice on stderr.\n"
    "--- stderr ---\n${err}")
endif()

# ...ON STDERR, AND NOT ON STDOUT. `-s`/`-x` stdout is a CI contract (main.cpp): a script
# that greps the monitor's output must not have to know about this feature at all.
if(out MATCHES "no machine named")
  message(FATAL_ERROR "cwdconfig: the notice landed on STDOUT and corrupted the monitor's "
                      "output.\n--- stdout ---\n${out}")
endif()

# ---- 2. AN EMPTY DIRECTORY still gets a machine. Silence keeps meaning `default`. ----
machine_in("${work}/empty" out err)
expect_machine("${out}" "swtpc" "with no ./swtpcsim.toml, a bare `swtpcsim` must still boot `swtpc`")

# ---- 3-5. THE NEGATIVE CONTROLS -- a NAMED machine is immune to the directory. ----
#
# If any of these three ever picks up cwd-discovered, the discovery has escaped the empty
# command line and `swtpcsim altair680` now means different things in different places.
# That is the trap looksLikeFile() was written to prevent, and this feature would have
# reintroduced it by the back door.
machine_in("${work}/proj" out err altair680)
expect_machine("${out}" "altair680" "a POSITIONAL built-in was overridden by ./swtpcsim.toml")

machine_in("${work}/proj" out err -m swtpc)
expect_machine("${out}" "swtpc" "`-m swtpc` was overridden by ./swtpcsim.toml")

machine_in("${work}/proj" out err -n)
expect_machine("${out}" "none" "`-n` (an EMPTY backplane) was overridden by ./swtpcsim.toml")

file(REMOVE_RECURSE "${work}")
message(STATUS "cwdconfig: ./swtpcsim.toml is found when nothing is named, and only then.")
