#!/bin/sh
# Build the pure-C engine and run map validation tests, including deliberately
# broken packs.  Set FRISK_SANITIZE=OFF to skip ASan/UBSan.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(dirname "$HERE")
BUILD="${FRISK_BUILD_DIR:-$ROOT/build}"
SANITIZE="${FRISK_SANITIZE:-ON}"
PACK="$ROOT/res/maps/lotr"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
      -DFRISK_SANITIZE="$SANITIZE" >/dev/null
cmake --build "$BUILD" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" \
      >/dev/null

echo "== frisk_tests =="
"$BUILD/frisk_tests" "$PACK"

echo "== mapcheck (valid pack) =="
"$BUILD/mapcheck" "$PACK"

echo "== fuzz =="
FUZZ_GAMES="${FRISK_FUZZ_GAMES:-10000}"
FUZZ_ARBITRARY="${FRISK_FUZZ_ARBITRARY:-100000}"
"$BUILD/frisk_fuzz" "$FUZZ_GAMES" "$FUZZ_ARBITRARY"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for case in asymmetric orphan unknown_effect uncovered_territory \
            label_out_of_bounds broken_symbols; do
    cp -R "$PACK" "$TMP/$case"
    python3 -I "$HERE/mutate_pack.py" "$TMP/$case" "$case"
    if "$BUILD/mapcheck" "$TMP/$case" >/dev/null 2>&1; then
        echo "FAIL: $case was accepted by mapcheck" >&2
        exit 1
    fi
    echo "PASS: $case rejected"
done

if "$BUILD/mapcheck" "$TMP/does-not-exist" >/dev/null 2>&1; then
    echo "FAIL: a missing pack was accepted" >&2
    exit 1
fi
echo "PASS: missing pack rejected"
echo "All map tests passed."
