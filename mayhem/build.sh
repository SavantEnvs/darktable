#!/usr/bin/env bash
#
# mayhem/build.sh — build darktable's fuzz harnesses and the known-answer selftest.
#
# darktable's parsers live in lib_darktable, which needs GTK, rawspeed and ~20
# other libraries plus git submodules. The harnessed parsers only need glib,
# lcms2 and darktable's own headers, so they are compiled straight from the
# upstream sources (instrumented with $SANITIZER_FLAGS) and linked against
# mayhem/harnesses/dt_shim.c (the `darktable` global, the dt_print_ext() logger,
# dt_control_log()) and dt_imageio_shim.c (dt_mipmap_cache_alloc,
# dt_alloc_aligned, dt_util_normalize_path). config.h is generated from src/config.cmake.h the way
# CMake would, with every optional feature off.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
cd "$SRC"

B="$SRC/mayhem/build"      # gitignored (mayhem/.gitignore); recreated on every run
OUT="$SRC/mayhem/out"      # gitignored; the Mayhemfiles point here
mkdir -p "$B/gen" "$B/fuzz" "$B/repro" "$B/test" "$OUT"

# config.h as CMake would generate it: @VARS@ -> 0/"" and #cmakedefine -> undefined
sed -e 's/(@[A-Za-z_]*@)/(0)/g' -e 's/@[A-Za-z_]*@//g' \
    -e 's|^#cmakedefine \([A-Za-z_0-9]*\).*|/* #undef \1 */|' \
    src/config.cmake.h > "$B/gen/config.h.tmp"
mv "$B/gen/config.h.tmp" "$B/gen/config.h"

PKGS="glib-2.0 gtk+-3.0 lcms2 librsvg-2.0 json-glib-1.0 sqlite3 libxml-2.0"
# src/common/imagebuf.c (dt_iop_image_copy, used by the PFM loader) also holds
# pixelpipe helpers that call into darktable's config and module code; one
# function per section plus --gc-sections drops those unused functions at link
# time instead of stubbing what they call.
INC="$(pkg-config --cflags $PKGS) -I$B/gen -I$SRC/src -D_GNU_SOURCE -std=gnu11 -ffunction-sections"
LIBS="$(pkg-config --libs glib-2.0 lcms2) -lm -Wl,--gc-sections -Wl,-z,start-stop-gc"

DT_SRCS="src/common/calculator.c src/common/dng_opcode.c src/chart/colorchart.c
         src/common/gpx.c src/imageio/imageio_pnm.c src/imageio/imageio_rgbe.c
         src/imageio/imageio_qoi.c src/imageio/imageio_pfm.c src/common/pfm.c
         src/common/imagebuf.c src/common/matrices.c
         mayhem/harnesses/dt_shim.c mayhem/harnesses/dt_imageio_shim.c"

# 1) project sources + harnesses, sanitized, DWARF 3. Two object sets: the
#    fuzzer build adds SanitizerCoverage (-fsanitize=fuzzer-no-link) so libFuzzer
#    sees edges in darktable's code; the -standalone reproducers are plain.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/repro/standalone_main.o"
fobjs=() ; robjs=()
for f in $DT_SRCS; do
  n="$(basename "${f%.c}")"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 $INC -c "$f" -o "$B/fuzz/$n.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $INC -c "$f" -o "$B/repro/$n.o"
  fobjs+=("$B/fuzz/$n.o") ; robjs+=("$B/repro/$n.o")
done

for h in fuzz_dng_opcode fuzz_calculator fuzz_chart fuzz_gpx fuzz_imageio; do
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 $INC -c "mayhem/harnesses/$h.c" -o "$B/fuzz/$h.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $INC -c "mayhem/harnesses/$h.c" -o "$B/repro/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/$h.o" "${fobjs[@]}" \
    "$B/fuzz/lsan_off.o" $LIBS -o "$OUT/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$B/repro/standalone_main.o" "$B/repro/$h.o" "${robjs[@]}" \
    "$B/fuzz/lsan_off.o" $LIBS -o "$OUT/$h-standalone"
done

# 2) the known-answer selftest, with normal flags (functional oracle for test.sh)
tobjs=()
for f in $DT_SRCS mayhem/tests/selftest.c; do
  o="$B/test/$(basename "${f%.c}").o"
  $CC -O2 $COVERAGE_FLAGS $INC -c "$f" -o "$o"
  tobjs+=("$o")
done
$CC $COVERAGE_FLAGS "${tobjs[@]}" $LIBS -o "$OUT/dt_selftest"
echo "build.sh: done"
