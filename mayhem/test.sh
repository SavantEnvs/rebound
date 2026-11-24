#!/usr/bin/env bash
# mayhem/test.sh — run the known-answer self-test built by mayhem/build.sh (normal flags) and
# report CTRF counts. Every expected check must print PASS; missing lines count as failures.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN=build/mayhem-test/rebound_selftest
EXPECTED="kepler_period_whfast kepler_period_ias15 kepler_period_leapfrog kepler_period_saba orbit_roundtrip orbit_circular energy_ias15 archive_nblobs archive_snapshot0 archive_snapshot_last archive_continue"
if [ ! -x "$BIN" ]; then
  echo "test.sh: $BIN missing — mayhem/build.sh did not build it" >&2
  n=$(wc -w <<<"$EXPECTED"); emit_ctrf rebound-selftest 0 "$n"; exit 1
fi
out="$("$BIN" 2>&1)"; rc=$?
echo "$out"
passed=0; failed=0
for t in $EXPECTED; do
  if grep -qx "PASS $t" <<<"$out"; then passed=$((passed+1)); else echo "FAILED: $t"; failed=$((failed+1)); fi
done
[ $rc -eq 0 ] || [ $failed -gt 0 ] || failed=1
emit_ctrf rebound-selftest "$passed" "$failed"
