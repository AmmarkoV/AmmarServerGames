#!/bin/sh
# Central test entry point for the AmmarServerGames repository.
#
# Each game keeps its own test suite (frisk: frisk/tests/run_tests.sh).  The
# parent AmmarServer checkout exposes this script through CTest, but it can
# also be run directly:
#
#     src/AmmarServerGames/run_tests.sh
#
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STATUS=0

run_suite()
{
    name="$1"
    script="$2"

    if [ ! -x "$script" ]; then
        echo "SKIP: $name (no test suite at $script)"
        return 0
    fi
    echo "== $name =="
    if "$script"; then
        return 0
    fi
    STATUS=1
    return 1
}

run_suite frisk "$HERE/frisk/tests/run_tests.sh"
run_suite "frisk map editor" "$HERE/frisk/tests/map_editor_test.py"
if [ -x "$HERE/frisk/frisk" ]; then
    run_suite "frisk HTTP smoke" "$HERE/frisk/tests/http_smoke.sh"
    run_suite "frisk authorization audit" "$HERE/frisk/tests/auth_audit.py"
    run_suite "frisk history and factions" "$HERE/frisk/tests/history_test.py"
    run_suite "frisk browser input" "$HERE/frisk/tests/browser_input.py"
    run_suite "frisk printed cards (browser)" "$HERE/frisk/tests/browser_cards.py"
else
    echo "SKIP: frisk HTTP smoke (server not built)"
    echo "SKIP: frisk authorization audit (server not built)"
    echo "SKIP: frisk history and factions (server not built)"
    echo "SKIP: frisk browser input (server not built)"
    echo "SKIP: frisk printed cards (browser) (server not built)"
fi
# score4 has no automated test suite yet; add it here when it does.

if [ "$STATUS" -eq 0 ]; then
    echo "All AmmarServerGames tests passed."
else
    echo "Some AmmarServerGames tests failed." >&2
fi
exit "$STATUS"
