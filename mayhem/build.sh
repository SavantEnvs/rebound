#!/usr/bin/env bash
# mayhem/build.sh — build REBOUND (librebound) sanitized + the fuzz harnesses, and a normal-flags
# build of the known-answer self-test that mayhem/test.sh runs. Re-runnable, no network.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS
cd "$SRC"

# The library sources, exactly as upstream's src/Makefile lists them (SERVER on, as upstream's
# Makefile.defs defaults; no OpenGL/MPI/OpenMP).
SOURCES="rebound.c tree.c particle.c gravity.c integrator_whfast.c integrator_whfast512.c integrator_saba.c integrator_ias15.c integrator_sei.c integrator_bs.c integrator_leapfrog.c integrator_mercurius.c integrator_trace.c integrator_eos.c boundary.c binarydata.c output.c collision.c communication_mpi.c display.c tools.c rotations.c simulation.c derivatives.c simulationarchive.c glad.c integrator_janus.c transformations.c fmemopen.c server.c frequency_analysis.c"
DEFS="-std=c99 -D_GNU_SOURCE -DSERVER -DLIBREBOUND"

# build_lib <outdir> <cflags...> : compile every source + the AVX-512 kernel into <outdir>/librebound.a
build_lib() {
  local out="$1"; shift
  mkdir -p "$out"
  local objs=()
  for s in $SOURCES; do objs+=("$out/${s%.c}.o"); done
  local pids=() s
  for s in $SOURCES; do
    local extra=""
    # reb_simulation_error/warning(NULL, msg) evaluates &r->messages on a NULL r (simulation.c:76-86)
    # for EVERY malformed archive (binarydata.c reb_binarydata_process_warnings(NULL, ...)); the
    # pointer is never dereferenced. UBSan's `null` check would stop every corrupt input there, so
    # it is relaxed for this one file only; ASan and all other UBSan checks stay on and halting.
    case "$s:$*" in simulation.c:*-fsanitize=*undefined*) extra="-fno-sanitize=null";; esac
    "$CC" "$@" $extra $DEFS -c "src/$s" -o "$out/${s%.c}.o" &
    pids+=($!)
  done
  for p in "${pids[@]}"; do wait "$p"; done
  case "$(uname -m)" in x86_64|amd64)
    "$CC" -c src/integrator_whfast512.s -o "$out/integrator_whfast512_asm.o"
    objs+=("$out/integrator_whfast512_asm.o");;
  esac
  rm -f "$out/librebound.a"
  ar rcs "$out/librebound.a" "${objs[@]}"
}

FUZZ_OUT=build/mayhem-fuzz
TEST_OUT=build/mayhem-test

# 1) sanitized + instrumented library
build_lib "$FUZZ_OUT" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$FUZZ_OUT/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_OUT/standalone_main.o"

# 2) harnesses: libFuzzer binary + standalone reproducer each
for h in fuzz_archive_parse fuzz_archive_step fuzz_orbit_integrate fuzz_server_http; do
  "$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -Isrc -Imayhem/harnesses -c "mayhem/harnesses/$h.c" -o "$FUZZ_OUT/$h.o"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_OUT/$h.o" "$FUZZ_OUT/lsan_off.o" \
    "$FUZZ_OUT/librebound.a" -lm -lpthread -o "/mayhem/$h"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$FUZZ_OUT/standalone_main.o" "$FUZZ_OUT/$h.o" "$FUZZ_OUT/lsan_off.o" \
    "$FUZZ_OUT/librebound.a" -lm -lpthread -o "/mayhem/$h-standalone"
done

# 3) normal-flags build for the functional oracle (known-answer self-test)
build_lib "$TEST_OUT" -O2 $COVERAGE_FLAGS
"$CC" -O2 $COVERAGE_FLAGS -std=c99 -D_GNU_SOURCE -Isrc mayhem/selftest.c "$TEST_OUT/librebound.a" \
  -lm -lpthread -o "$TEST_OUT/rebound_selftest"
echo "build.sh: done"
