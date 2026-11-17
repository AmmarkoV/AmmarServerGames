#!/bin/sh
# End-to-end account, invite, remember-me, game persistence and M8 chat/clip
# smoke test.
#
# Requires the frisk server binary from a parent AmmarServer build (the frisk
# target writes it next to this repository's CMakeLists.txt).  The test uses
# a private temporary users database and data directory, never the live one.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FRISK_DIR=$(dirname "$HERE")
AMMAR_ROOT=$(CDPATH= cd -- "$FRISK_DIR/../../.." && pwd)
SERVER_BIN="${FRISK_SERVER_BIN:-$FRISK_DIR/frisk}"
PORT="${FRISK_SMOKE_PORT:-18090}"
BASE="http://127.0.0.1:$PORT"
TMP=$(mktemp -d)
SERVER_PID=""
FAILURES=0

cleanup()
{
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1" >&2; FAILURES=$((FAILURES + 1)); }

need() { # description, haystack, needle
    case "$2" in
        *"$3"*) pass "$1" ;;
        *) fail "$1 (got: $2)" ;;
    esac
}

json_value() { # field, json
    printf '%s' "$2" | sed -n 's/.*"'"$1"'":"\([^"]*\)".*/\1/p'
}

start_server()
{
    # Keep uploads and chat out of the source tree, while still serving clips
    # from inside the web root ( AmmarServer refuses to stream outside it ).
    RES="$TMP/res"
    if [ ! -d "$RES/maps" ]; then
        mkdir -p "$RES/uploads/clips" || exit 1
        cp "$FRISK_DIR/res/index.html" "$RES/index.html" || exit 1
        cp -R "$FRISK_DIR/res/maps" "$RES/maps" || exit 1
    fi
    export FRISK_RES_ROOT="$RES/"
    export FRISK_CLIPS_DIR="$RES/uploads/clips"
    export FRISK_DATA_DIR="$TMP/data"
    export FRISK_USERS_DB="$TMP/users.db"
    export FRISK_PORT="$PORT"
    cd "$AMMAR_ROOT" || exit 1
    "$SERVER_BIN" >>"$TMP/server.log" 2>&1 &
    SERVER_PID=$!
    i=0
    while [ "$i" -lt 40 ]; do
        if curl -s --max-time 1 "$BASE/api/me" 2>/dev/null | grep -q '"ok":1'; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.2
    done
    echo "server did not start; log follows:" >&2
    tail -40 "$TMP/server.log" >&2
    return 1
}

stop_server()
{
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    fi
}

stop_server_hard()
{
    if [ -n "$SERVER_PID" ]; then
        kill -9 "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    fi
}

if [ ! -x "$SERVER_BIN" ]; then
    echo "SKIP: $SERVER_BIN is not built." >&2
    echo "Build it with: cmake --build <parent-build> --target frisk" >&2
    exit 0
fi

start_server || exit 1

# 1. Anonymous view.
BODY=$(curl -s --max-time 4 "$BASE/api/me")
need "anonymous /api/me" "$BODY" '"user":null'

# 2. The first account bootstraps without an invite and becomes admin.
JAR1="$TMP/jar1"
BODY=$(curl -s --max-time 4 -c "$JAR1" -b "$JAR1" \
    -F "name=admin" \
    -F "password=password123" \
    "$BASE/api/register")
need "bootstrap registration" "$BODY" '"ok":1'
need "bootstrap admin" "$BODY" '"admin":true'
CSRF=$(json_value csrf "$BODY")
[ -n "$CSRF" ] && pass "registration returned csrf" || fail "missing csrf"

BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/me")
need "session after registration" "$BODY" '"user":"admin"'

# 3. Registration without an invite is refused now that the database is not empty.
BODY=$(curl -s --max-time 4 -F "name=nobody" \
    -F "password=password123" "$BASE/api/register")
need "invite required" "$BODY" '"err":"invite"'

# 4. Create an invite and use it once.
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    "$BASE/api/invite/new")
need "invite creation" "$BODY" '"ok":1'
CODE=$(json_value code "$BODY")
[ -n "$CODE" ] && pass "invite code returned" || fail "missing invite code"

JAR2="$TMP/jar2"
BODY=$(curl -s --max-time 4 -c "$JAR2" -b "$JAR2" \
    -F "name=bob" \
    -F "password=password123" \
    -F "invite=$CODE" "$BASE/api/register")
need "registration with invite" "$BODY" '"ok":1'
need "second user is not admin" "$BODY" '"admin":false'
CSRF2=$(json_value csrf "$BODY")
[ -n "$CSRF2" ] && pass "second user got csrf" || fail "missing second csrf"

BODY=$(curl -s --max-time 4 \
    -F "name=carol" \
    -F "password=password123" \
    -F "invite=$CODE" "$BASE/api/register")
need "invite reuse refused" "$BODY" '"err":"invite"'

BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    "$BASE/api/invite/new")
need "second invite creation" "$BODY" '"ok":1'
CODE2=$(json_value code "$BODY")
JAR3="$TMP/jar3"
BODY=$(curl -s --max-time 4 -c "$JAR3" -b "$JAR3" \
    -F "name=carol" \
    -F "password=password123" \
    -F "invite=$CODE2" "$BASE/api/register")
need "spectator registration" "$BODY" '"ok":1'
CSRF3=$(json_value csrf "$BODY")
[ -n "$CSRF3" ] && pass "spectator got csrf" || fail "missing spectator csrf"

# 5. State-changing POST without csrf is refused.
BODY=$(curl -s --max-time 4 -b "$JAR1" \
    -F "colour=#ff0000" "$BASE/api/profile/set")
need "missing csrf refused" "$BODY" '"err":"csrf"'

# 6. Static shell and map discovery.
BODY=$(curl -s --max-time 4 "$BASE/")
need "index.html served" "$BODY" "FRISK"
BODY=$(curl -s --max-time 4 "$BASE/api/map")
need "map packs listed" "$BODY" '"lotr"'

# 7. M8: lobby text chat, rate limits and taunts.
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F 'text=smoke says hello' "$BASE/api/chat/say")
need "lobby text accepted" "$BODY" '"ok":1'
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=lobby")
need "lobby text tail" "$BODY" 'smoke says hello'
need "chat tail reports taunt count" "$BODY" '"taunts":8'
RATE_SEEN=0
i=0
while [ "$i" -lt 6 ]; do
    BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
        -F "room=lobby" -F "text=spam $i" "$BASE/api/chat/say")
    case "$BODY" in
        *'"err":"rate"'*) RATE_SEEN=1; break ;;
    esac
    i=$((i + 1))
done
[ "$RATE_SEEN" = "1" ] && pass "text rate limit" || fail "text rate limit not hit"
BODY=$(curl -s --max-time 4 -b "$JAR2" -F "csrf=$CSRF2" \
    -F "room=lobby" -F "taunt=3" "$BASE/api/chat/say")
need "taunt accepted" "$BODY" '"ok":1'
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=lobby")
need "taunt appears in chat" "$BODY" '"kind":"taunt"'

# 7. Create, join, start and play a game over HTTP.
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "name=Smoke Game" -F "map=lotr" -F "seats=2" \
    -F "mode=deal" -F "batch=5" "$BASE/api/game/create")
need "game creation" "$BODY" '"ok":1'
GID=$(printf '%s' "$BODY" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p')
[ -n "$GID" ] && pass "game id returned" || fail "missing game id"

BODY=$(curl -s --max-time 4 -b "$JAR2" -F "csrf=$CSRF2" \
    -F "g=$GID" "$BASE/api/game/join")
need "second player joins" "$BODY" '"ok":1'

BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "g=$GID" "$BASE/api/game/start")
need "host starts game" "$BODY" '"ok":1'

# M8: game room system lines, seated posting and spectator read-only access.
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=g$GID")
need "game join system line" "$BODY" 'joined the game'
need "game start system line" "$BODY" 'The game has started.'
sleep 1
BODY=$(curl -s --max-time 4 -b "$JAR2" -F "csrf=$CSRF2" \
    -F "room=g$GID" -F "text=to the walls" "$BASE/api/chat/say")
need "seated player posts in game room" "$BODY" '"ok":1'
BODY=$(curl -s --max-time 4 -b "$JAR3" "$BASE/api/chat?room=g$GID")
need "spectator reads game room" "$BODY" 'to the walls'
BODY=$(curl -s --max-time 4 -b "$JAR3" -F "csrf=$CSRF3" \
    -F "room=g$GID" -F "text=spectator noise" "$BASE/api/chat/say")
need "spectator cannot post" "$BODY" '"err":"seat"'

# Seat order is shuffled at start, so find which player holds the current turn.
STATE1=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/game/state?g=$GID")
STATE2=$(curl -s --max-time 4 -b "$JAR2" "$BASE/api/game/state?g=$GID")
HAS_LEGAL=$(printf '%s' "$STATE1" | python3 -c \
    'import json,sys; print(1 if json.load(sys.stdin).get("legal") else 0)')
if [ "$HAS_LEGAL" = "1" ]; then
    STATE="$STATE1"; ACTIVE_JAR="$JAR1"; ACTIVE_CSRF="$CSRF"
else
    STATE="$STATE2"; ACTIVE_JAR="$JAR2"; ACTIVE_CSRF="$CSRF2"
fi
need "game is running" "$STATE" '"status":"running"'
need "setup placement phase" "$STATE" '"phase":"setup_place"'
need "board has 64 territories" "$STATE" '"territories":[{"owner"'
PLACE=$(printf '%s' "$STATE" | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["legal"]["placeable"][0])')
[ -n "$PLACE" ] && pass "legal placement returned" || fail "no legal placement"
GVERSION=$(printf '%s' "$STATE" | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["version"])')

BODY=$(curl -s --max-time 4 -b "$ACTIVE_JAR" -F "csrf=$ACTIVE_CSRF" -F "g=$GID" \
    -F "type=place" -F "t=$PLACE" -F "count=1" "$BASE/api/game/act")
need "placement action accepted" "$BODY" '"ok":1'

STATE=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/game/state?g=$GID")
need "action reached the log" "$STATE" 'placed 1 armies'
GVERSION2=$(printf '%s' "$STATE" | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["version"])')
[ "$GVERSION2" -gt "$GVERSION" ] && pass "game version advanced" || \
    fail "game version did not advance"

# 8. M6: durable log, after=log replay and the seen marker.
LOGFILE="$TMP/data/games/game_$GID.log"
[ -s "$LOGFILE" ] && pass "durable game log exists" || \
    fail "durable game log is missing"
LOGTEXT=$(cat "$LOGFILE" 2>/dev/null)
need "durable log has the placement" "$LOGTEXT" '"kind":"place"'
BODY=$(curl -s --max-time 4 -b "$JAR1" \
    "$BASE/api/game/log?g=$GID&after=0&limit=100")
need "log after=0 serves history" "$BODY" '"kind":"start"'
need "log events carry a version" "$BODY" '"version":'
need "log after=0 includes the placement" "$BODY" '"kind":"place"'

BODY=$(curl -s --max-time 4 -b "$ACTIVE_JAR" -F "csrf=$ACTIVE_CSRF" \
    -F "g=$GID" -F "type=seen" "$BASE/api/game/act")
need "seen marker accepted" "$BODY" '"ok":1'
STATE=$(curl -s --max-time 4 -b "$ACTIVE_JAR" "$BASE/api/game/state?g=$GID")
SEEN_OK=$(printf '%s' "$STATE" | python3 -c \
    'import json,sys; g=json.load(sys.stdin); print(1 if g["players"][g["you"]]["lastSeen"] >= g["version"] else 0)')
[ "$SEEN_OK" = "1" ] && pass "seen marker stored the version" || \
    fail "seen marker was not stored"

# 8b. M8: clip magic sniffing, strict serving, rate limiting and rejection of
#     renamed executables/markup or oversized uploads.
python3 - "$TMP/clip.wav" <<'PY'
import struct
import sys
import wave

with wave.open(sys.argv[1], "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(8000)
    wav.writeframes(b"".join(
        struct.pack("<h", 2000 if (i % 80) < 40 else -2000)
        for i in range(1600)
    ))
PY
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F "dur=200" \
    -F "clip=@$TMP/clip.wav;filename=evil.exe;type=application/octet-stream" \
    "$BASE/api/chat/clip")
need "clip upload accepted" "$BODY" '"ok":1'
CLIP=$(json_value file "$BODY")
[ -n "$CLIP" ] && pass "clip name returned" || fail "missing clip name"
case "$CLIP" in
    *.wav) pass "clip extension came from the sniff, not the filename" ;;
    *) fail "clip extension was not derived from content ($CLIP)" ;;
esac
curl -s --max-time 4 -D "$TMP/clip.headers" -o "$TMP/clip.got" \
    -b "$JAR1" "$BASE/api/clip?f=$CLIP"
grep -qi '^Content-type: audio/wav' "$TMP/clip.headers" && \
    pass "clip served with audio content type" || \
    fail "clip content type was not audio/wav"
cmp "$TMP/clip.wav" "$TMP/clip.got" && \
    pass "clip bytes round-tripped" || fail "clip bytes changed"
BODY=$(curl -s --max-time 4 "$BASE/api/clip?f=$CLIP")
need "unauthenticated clip fetch refused" "$BODY" '"err":"login"'
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/clip?f=..%2Fusers.db")
need "path traversal refused" "$BODY" '"err":"clip"'
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" "$BASE/api/chat/say")
need "empty chat message refused" "$BODY" '"err":"text"'

dd if=/dev/zero of="$TMP/oversized.bin" bs=1024 count=513 2>/dev/null
BODY=$(curl -s --max-time 8 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F "clip=@$TMP/oversized.bin;filename=big.wav" \
    "$BASE/api/chat/clip")
need "oversized clip refused" "$BODY" '"err":"toolarge"'
printf 'MZ\220\000\003' > "$TMP/renamed.exe"
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F "clip=@$TMP/renamed.exe;filename=evil.mp3" \
    "$BASE/api/chat/clip")
need "renamed executable refused" "$BODY" '"err":"clip"'
printf '<!doctype html><script>alert(1)</script>' > "$TMP/renamed.html"
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F "clip=@$TMP/renamed.html;filename=evil.mp3" \
    "$BASE/api/chat/clip")
need "renamed HTML refused" "$BODY" '"err":"clip"'
BODY=$(curl -s --max-time 4 -b "$JAR1" -F "csrf=$CSRF" \
    -F "room=lobby" -F "clip=@$TMP/clip.wav;filename=again.wav" \
    "$BASE/api/chat/clip")
need "clip rate limit" "$BODY" '"err":"rate"'
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=lobby")
need "clip appears in chat" "$BODY" "$CLIP"
need "clip duration stored" "$BODY" '"dur":200'

# Other accepted container types get the audio content type, not video.
python3 - "$TMP/clip.webm" <<'PY'
import sys
open(sys.argv[1], "wb").write(b"\x1a\x45\xdf\xa3\x01\x00\x00\x00")
PY
BODY=$(curl -s --max-time 4 -b "$JAR2" -F "csrf=$CSRF2" \
    -F "room=lobby" -F "clip=@$TMP/clip.webm;filename=voice.bin" \
    "$BASE/api/chat/clip")
need "webm clip accepted" "$BODY" '"ok":1'
WEBA=$(json_value file "$BODY")
case "$WEBA" in
    *.weba) pass "webm extension came from the sniff" ;;
    *) fail "webm extension was not derived from content ($WEBA)" ;;
esac
curl -s --max-time 4 -D "$TMP/webm.headers" -o /dev/null \
    -b "$JAR1" "$BASE/api/clip?f=$WEBA"
grep -qi '^Content-type: audio/webm' "$TMP/webm.headers" && \
    pass "webm served as audio/webm" || \
    fail "webm content type was not audio/webm"

python3 - "$TMP/clip.m4a" <<'PY'
import sys
open(sys.argv[1], "wb").write(b"\x00\x00\x00\x18ftypM4A \x00\x00\x00\x00")
PY
BODY=$(curl -s --max-time 4 -b "$JAR3" -F "csrf=$CSRF3" \
    -F "room=lobby" -F "clip=@$TMP/clip.m4a;filename=voice.mp4" \
    "$BASE/api/chat/clip")
need "m4a clip accepted" "$BODY" '"ok":1'
M4A=$(json_value file "$BODY")
case "$M4A" in
    *.m4a) pass "m4a extension came from the sniff" ;;
    *) fail "m4a extension was not derived from content ($M4A)" ;;
esac
curl -s --max-time 4 -D "$TMP/m4a.headers" -o /dev/null \
    -b "$JAR1" "$BASE/api/clip?f=$M4A"
grep -qi '^Content-type: audio/mp4' "$TMP/m4a.headers" && \
    pass "m4a served as audio/mp4" || \
    fail "m4a content type was not audio/mp4"

# 9. Restart: sessions are in-memory, the remember-me cookie must restore
#    login, and an ungraceful kill must not lose the saved game.
stop_server_hard
start_server || exit 1
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/me")
need "remember-me survives restart" "$BODY" '"user":"admin"'
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=lobby&limit=100")
need "lobby chat survives kill -9" "$BODY" 'smoke says hello'
need "taunt survives kill -9" "$BODY" '"kind":"taunt"'
need "clip chat line survives kill -9" "$BODY" "$CLIP"
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/chat?room=g$GID")
need "game chat survives kill -9" "$BODY" 'to the walls'
curl -s --max-time 4 -o "$TMP/clip.after" -b "$JAR1" \
    "$BASE/api/clip?f=$CLIP"
cmp "$TMP/clip.wav" "$TMP/clip.after" && \
    pass "clip still plays after kill -9" || \
    fail "clip did not survive kill -9"
BODY=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/invite/list")
need "invites survive restart" "$BODY" "$CODE"
STATE=$(curl -s --max-time 4 -b "$JAR1" "$BASE/api/game/state?g=$GID")
need "game survives kill -9" "$STATE" '"status":"running"'
need "placed armies survive kill -9" "$STATE" 'placed 1 armies'
GVERSION3=$(printf '%s' "$STATE" | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["version"])')
[ "$GVERSION3" = "$GVERSION2" ] && pass "game version restored" || \
    fail "game version changed across restart ($GVERSION2 -> $GVERSION3)"
BODY=$(curl -s --max-time 4 -b "$JAR1" \
    "$BASE/api/game/log?g=$GID&after=0&limit=100")
need "durable log survives kill -9" "$BODY" '"kind":"start"'
LOGTEXT=$(cat "$LOGFILE" 2>/dev/null)
need "durable log file survives kill -9" "$LOGTEXT" '"kind":"place"'

if [ "$FAILURES" -ne 0 ]; then
    echo "http_smoke: $FAILURES failure(s)" >&2
    exit 1
fi
echo "All http smoke tests passed."
exit 0
