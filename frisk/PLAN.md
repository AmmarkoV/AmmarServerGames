# FRISK — implementation plan

A browser-played, asynchronous, LAN-hosted territory-conquest board game for 2–4 players, built as an
AmmarServer service in C with a dependency-free HTML/JS client.

Status: **M1 (Phase A), M2 (Phase D), M3 (Phase F engine half), M4 (Phases B/C), M5
(playable lobby + board + browser client), M6 (async comfort), M7 (leaders, strongholds, sites,
adventure cards), M8 (chat, clips, taunts, housekeeping) and M9 (hardening, load test, README)
implemented.** Post-M9 polish (2026-11-17): 7.6 finished-game history/unloading, 8.4 faction pick,
8.5 last-action highlight + double-tap zoom, 8.7 dice animation, 8.11 responsive layout, 8.12
end-of-game banner and the 8.13 offline audit.
The default data pack is the owner's RISK: The Lord of the Rings board with PLAN.md's rules.
Tick the boxes as steps land; deviations from a step are recorded inline.

---

## 0. Decisions already taken

| Topic | Decision |
|---|---|
| Game content | **Default pack = RISK: The Lord of the Rings (Trilogy Edition) board/theme**; mechanics remain PLAN.md's (last player standing, deal/draft, generic adventure effects). All map/theme specifics live under `res/maps/<pack>/`; no territory, card or faction name is hardcoded in C or JS. An original pack can be dropped in later. |
| Play style | Asynchronous (turns may be hours/days apart) on a LAN server, zero internet dependencies (no CDN, no web fonts). |
| Rules scope | Full: reinforce / attack / fortify, territory cards, **leaders, strongholds, sites of power, mission + event + power cards**. No turn limit; win = last player standing. |
| Players | 2–4 humans. No AI. Spectators allowed (read-only, may read chat, may not post). |
| Accounts | `src/UserAccounts`, cookie sessions, **invite-only** registration. Any registered player can mint invite codes. First account ever created is admin. |
| Voice | Clips, not VoIP. Works over plain HTTP through `<input type=file accept="audio/*" capture>`; upgrades to in-page hold-to-record only when the page is a secure context (HTTPS / localhost). |
| Transport | Polling (AmmServerlib has no WebSocket/SSE). |
| Persistence | Everything survives a restart: accounts, invites, profiles, games, chat, clips, login. |
| Existing files | `board.html`, `res/board.html`, `res/textures/*`, `res/sounds/*` may be edited/replaced. The generic sounds (`dice_1.wav`, `win.wav`, `lost.wav`, `intro.wav`, `battle.ogg`) are reused. |
| Git | No commits unless explicitly requested. |

### Facts about the codebase this plan relies on (verified)

- `Services/Social` is the closest working template: session-enabled handlers (`addSessionResourceHandler`,
  `context->requestContext.useSessionLifecycle=1`, `AmmServer_DoNOTCacheResourceHandler`), CSRF
  (`AmmServer_GenerateCSRFToken` / `AmmServer_ValidateCSRFToken`), uploads (`_FILES(rqst,field,VALUE,&size)` with
  `ENABLE_RECEIVING_FILES`, magic-byte sniffing in `media.c`), `pthread_mutex` around shared state.
- `UserAccounts/userAccountsWeb.h`: `AmmServer_Login`, `AmmServer_Logout`, `AmmServer_CurrentUsername`,
  `uadbWeb_isValidUsername` (3–31 chars, `[A-Za-z0-9_-]`), `uadbWeb_userAccountExists`, `uadb_addUser`.
- AmmServerlib sessions (`cache/session_list.h`) are in-memory with idle eviction → they do **not** survive a
  restart. We add our own persistent remember-me token (step 4.5).
- Response buffers are fixed size per handler (`allocate_mem_bytes`); handlers must never write past
  `rqst->MAXcontentSize`.
- POST body cap is `AMMSET_MAX_POST_TRANSACTION_SIZE` (settable via `AmmServer_SetIntSettingValue`).
- Static MIME table knew `mp3/wav/ogg` audio but not `webm/m4a/3gp`; M8 added `weba`, `m4a` and `amr` to the
  audio scanner/table (step 9.4). Clips are still streamed through the dynamic handler.
- `AmmServer_AddScheduler` exists but currently delegates to a stub `schedulerAdd`; M8 uses a dedicated
  housekeeping thread instead (step 9.7).
- Handlers run on multiple threads → one global `pthread_mutex_t friskLock` guards all game/lobby state.
- `score4/CMakeLists.txt` and its `webserver_root` (`src/Games/score4/res/`) are stale; do not copy them —
  copy `Services/Social/CMakeLists.txt` instead.

### Things still to verify during implementation (each has a step)

- [x] Default and maximum usable value of `AMMSET_MAX_POST_TRANSACTION_SIZE` (step 9.1). **Verified:** the
      default is 4 MiB, above the 512 KiB clip cap, so no change was needed.
- [x] Whether `AmmServer_DynamicRequestReturnFile` lets us control the Content-Type for `.webm/.m4a` (step 9.4).
      **Verified:** the file extension controls it; `.webm` hits the video table first, so WebM audio is stored
      as `.weba` and the AmmarServerlib audio table now maps `weba`/`m4a`/`amr`.
- [x] Whether a handler can return a non-200 status via `rqst->headerResponse` (step 3.3). **Verified:** the
      library copies it into the response status; the HTTP layer uses it for 400/401/403/404/409/429/500/501.
- [x] Whether `UserAccounts` is thread-safe for concurrent `uadb_addUser` (step 4.2). **Verified:** it mutates
      `userList` in place, so every account operation goes through `friskLock`.

---

## 1. Directory layout (target)

```
frisk/
  PLAN.md                    this file
  CMakeLists.txt             modelled on Services/Social/CMakeLists.txt
  main.c                     server start, handler registration, shutdown
  http.c / http.h            JSON response helpers, auth + CSRF guards, param parsing
  accounts.c / accounts.h    login, register, invites, remember-me, profiles
  lobby.c / lobby.h          game list, create/join/leave/start/kick
  play.c / play.h            state.json + action endpoint (HTTP <-> engine glue)
  chat.c / chat.h            text chat, clip upload, clip serving
  store.c / store.h          atomic file save/load of games, profiles, invites, chat
  json.c / json.h            tiny JSON writer + minimal reader (objects/arrays/strings/ints)
  engine/
    map.c / map.h            map pack loading + validation
    rules.c / rules.h        game state struct, phase machine, action validation + application
    cards.c / cards.h        decks, territory-card sets, mission/event/power effect table
    dice.c / dice.h          RNG (getrandom) + injectable deterministic RNG for tests
    serialize.c / .h         game state <-> JSON
  tests/
    test_map.c  test_rules.c  test_cards.c  test_serialize.c  fuzz_games.c
    run_tests.sh
    http_smoke.sh            curl-driven end-to-end script
  tools/
    mapcheck.c               CLI: validate a map pack
    mkmap.py                 generator for the original default map (run once, output committed)
  res/                       web root
    index.html               single page app shell
    css/frisk.css
    js/api.js  app.js  lobby.js  game.js  map.js  dice.js  cards.js  chat.js  voice.js  replay.js
    maps/default/            the original data pack (see step 2); the first shipped pack is
                             lotr/ instead (owner decision), any folder works
    sounds/                  existing generic sounds + taunts/
    textures/                existing environment textures (wood.jpg) + piece sprites
db/frisk/                    runtime data (created at startup, relative to AmmarServer root)
    invites.db  profiles.db  remember.db
    games/game_<id>.json     one file per game (active + finished)
    chat/lobby.log  chat/game_<id>.log
    clips/<token>.<ext>
```

The engine directory must not include `AmmServerlib.h` — it is plain C so it can be compiled into the test
binaries and `mapcheck` without the server.

---

## 2. Phase A — Map data pack

Goal: the whole board is data. Changing the map/theme = dropping a new folder into `res/maps/`.

- [x] **2.1 Define `map.json` schema** (documented in `res/maps/README.md`):
  ```json
  {
    "name": "Default", "version": 1,
    "image": {"file":"board.svg","width":2000,"height":1400},
    "factions": [{"id":0,"name":"...","color":"#b03030","sprites":{"unit1":"...","unit3":"...","unit5":"...","leader":"..."}}, ...],
    "regions": [{"id":0,"name":"...","bonus":4,"color":"#..."}],
    "territories": [{"id":0,"name":"...","region":0,"path":"M...Z","label":[x,y],
                     "stronghold":false,"site":false,"adjacent":[1,2,7]}],
    "links": [{"a":3,"b":19,"kind":"sea","path":"M..."}],
    "territoryCards": [{"territory":0,"symbol":"A"}, ... , {"wild":true}],
    "adventureCards": [{"id":"m01","kind":"mission|event|power","title":"...","text":"...",
                        "effect":"EFFECT_ID","args":{...}}],
    "setup": {"2":{"armies":40,"neutral":true}, "3":{"armies":35}, "4":{"armies":30}},
    "rules": {"setBonus":{"AAA":4,"BBB":6,"CCC":8,"ABC":10}, "minReinforce":3, "territoriesPerArmy":3,
              "strongholdReinforce":1, "leadersPerPlayer":2}
  }
  ```
  `adjacent` lists land borders; `links` are extra adjacency (sea routes/bridges) drawn as dashed lines.
  All tunable numbers live in `rules` / `setup` so a different pack can rebalance without code changes.
- [x] **2.2 `engine/map.c`**: `map_load(path, struct Map*)`, static upper bounds
  (`MAX_TERRITORIES 96`, `MAX_REGIONS 16`, `MAX_ADJ 10`, `MAX_CARDS 128`), no heap after load.
- [x] **2.3 Validation** in `map_validate()`; every failure prints territory names:
  adjacency symmetric; graph connected; every territory in exactly one region; region bonus > 0;
  ≥ 1 stronghold and ≥ 1 site; territory-card deck covers every territory exactly once + wilds;
  three symbols roughly balanced (±1); every `effect` id exists in the effect table (step 6.4);
  `label` inside the image bounds; territory count ≥ 4 × leadersPerPlayer.
- [x] **2.4 `tools/mapcheck.c`** CLI wrapping 2.2/2.3. → verify: `mapcheck res/maps/lotr` exits 0;
  deliberately broken copies (asymmetric edge, orphan territory, unknown effect, missing card, bad label,
  unbalanced symbols) exit non-zero with a clear message. `tests/run_tests.sh` runs all of these under
  ASan/UBSan.
- [x] **2.5 Generate the default map pack** with `tools/mkmap.py`: the owner chose the RISK: The Lord of
  the Rings Trilogy Edition board as the first pack, so the generator emits the 64-territory / 9-region
  `lotr/` pack from the board scan. Territories use labelled anchor hexes because the printed borders are
  raster; `board.svg` has one `<path id="t<N>">` per territory. The generated output is checked in; the
  script is kept only for regeneration. (The plan's original 42-territory invented map can be added later
  as another pack.)
- [x] **2.6 Piece art**: simple original SVG tokens (unit ×1, ×3, ×5, leader, stronghold tower, site marker)
  tinted per faction colour via CSS `fill`, so no per-colour bitmaps are needed.
- [x] **2.7 Card content**: 64 territory cards + 2 wilds; 36 adventure cards (12 missions, 12 events,
  12 powers) written against the effect ids of step 6.4.

**Exit check A (passed):** `mapcheck` passes; opening `board.svg` in a browser (or rendering it with
Inkscape) shows the labelled board with one anchor hex per territory.

---

## 3. Phase B — Service skeleton

- [x] **3.1 `CMakeLists.txt`** copied from Social: target `frisk`, sources listed explicitly, links
  `pthread rt AmmarServerDynamic UserAccounts`. Second target `frisk_tests` (engine + tests, no server libs).
  Add `add_subdirectory(src/AmmarServerGames/frisk)` to the root `CMakeLists.txt` next to the Services.
  **Implemented:** because AmmarServerGames is a separate git repository, the parent root includes
  `src/AmmarServerGames` when its `CMakeLists.txt` exists, otherwise `src/AmmarServerGames/frisk` when that
  exists, and ignores the tree entirely on checkouts without it. The `frisk` server target is created only
  when `AmmarServerDynamic` and `UserAccounts` targets exist; standalone frisk configures the engine and
  tests without the server libraries.
- [x] **3.2 `main.c`**: `AmmServer_StartWithArgs("Frisk",…, "0.0.0.0", 8090, 0, "src/AmmarServerGames/frisk/res/", "public_html/templates/")`;
  termination signal → flush + close; `init_dynamic_content()` registering every endpoint through a local
  `addSessionResourceHandler()` (same body as Social's). Create `db/frisk/**` directories if missing.
- [x] **3.3 `http.c` helpers**:
  - `jsonBegin/jsonEnd/jsonKV…` writing into `rqst->content` with bounds checks against `MAXcontentSize`;
    on overflow reply `{"ok":0,"err":"overflow"}`.
  - `requireUser(rqst, name)` → 0 + `{"ok":0,"err":"login"}` when not logged in.
  - `requireCSRF(rqst)` for every state-changing POST (field `csrf`).
  - `paramUint(rqst,"g",&v)` with range checks; all ids validated before indexing arrays.
  - Decide/verify the status-code question listed in §0.
- [x] **3.4 Endpoint table** (all JSON unless noted; every POST needs login + CSRF except login/register):

  | Method | Path | Purpose |
  |---|---|---|
  | GET | `/` → `index.html` | static SPA shell |
  | GET | `/api/me` | `{user, admin, csrf, profile}` or `{user:null}`; also performs remember-me re-login |
  | POST | `/api/login` `/api/logout` `/api/register` | accounts |
  | POST | `/api/invite/new` · GET `/api/invite/list` | invite codes |
  | GET | `/api/profile?u=` · POST `/api/profile/set` | stats, colour, avatar glyph |
  | GET | `/api/lobby?since=V` | games list + lobby chat tail |
  | POST | `/api/game/create` `/join` `/leave` `/start` `/kick` `/delete` | lobby actions |
  | GET | `/api/game/state?g=&since=V` | full visible state or `{"same":1}` |
  | GET | `/api/game/log?g=&from=N` | event log slice (replay) |
  | POST | `/api/game/act` | one game action (`type` + args) |
  | GET | `/api/chat?room=&after=N` · POST `/api/chat/say` | text chat |
  | POST | `/api/chat/clip` (multipart) · GET `/api/clip?f=` | voice clips |
  | GET | `/api/map?name=` | list packs / pack metadata (map.json itself is static) |

  **Implemented in M4/M5:** `/api/me`, `/api/login`, `/api/logout`, `/api/register`, the three invite
  endpoints, the two profile endpoints, `/api/map`, the lobby lists/create/join/leave/start/kick/delete
  endpoints, the game state/log/act endpoints and the chat/clip endpoints are live. POST handlers expect
  multipart/form-data, matching AmmarServerlib's parser. Note that an empty multipart field before `csrf`
  stops AmmarServerlib's parser, so the client omits empty optional fields and sends `csrf` before a clip
  file part.

- [x] **3.5 Locking rule**: every handler takes `friskLock` for its whole body except file streaming of clips.
  Handlers are short (no blocking I/O other than the atomic save), so one coarse lock is enough for ≤ dozens of
  players. Document this at the top of `main.c`.
- [x] **3.6 Start/stop integration**: add frisk to `startAmmarServerSuite.sh` / `stopAmmarServerSuite.sh`
  following the existing entries.

**Exit check B (passed):** `cmake --build <parent> --target frisk` produces `frisk`; `curl :8090/api/me` →
`{"ok":1,"user":null}`; static `index.html` is served.

---

## 4. Phase C — Accounts, invites, profiles

- [x] **4.1 `accounts.c` init**: `uadb_initializeUserAccountDatabase("db/users.db")` — the same file Social
  uses, so accounts are shared across AmmarServer services (make the path a `#define`; switch to
  `db/frisk/users.db` if isolation is preferred).
  **Implemented:** the path is configurable with `FRISK_USERS_DB` (default `db/users.db`). When the shared
  database already has users, the first frisk login/profile created becomes the frisk admin, so an existing
  AmmarServer account can bootstrap invites.
- [x] **4.2 Register** `POST /api/register {name,password,invite}`:
  validate name with `uadbWeb_isValidUsername`, password length 8–100, invite exists & unused → `uadb_addUser`
  → mark invite used (by whom, when) → `AmmServer_Login`. Bootstrap: when the user database is empty, no invite
  is required and the account becomes admin (`profiles.db` flag). Uniform error for bad/used code. Rate-limit:
  on 5 failures per IP per 10 min call `AmmServer_SignalCountAsBadClientBehaviour`.
  **Implemented:** the rate limiter keys on `rqst->clientID` because dynamic callbacks do not receive the
  peer IP; the attempt table is under `friskLock`.
- [x] **4.3 Invites** `invites.db` (text, one per line: `code creator created usedBy usedAt`). Codes are
  10 chars from an unambiguous alphabet, generated from `getrandom`. `POST /api/invite/new` (max 5 outstanding
  per user), `GET /api/invite/list` (own codes + status). Admin can revoke.
- [x] **4.4 Login/logout**: thin wrappers around `AmmServer_Login` / `AmmServer_Logout`; same failure
  rate-limit as 4.2.
- [x] **4.5 Remember-me (restart survival)**: on login issue cookie `frisk_rm=<selector>.<validator>`
  (`AmmServer_SetCookie`, 30 days, httpOnly). Store `selector sha256(validator) username expiry` in
  `remember.db`. In `requireUser`, when the session has no username but a valid `frisk_rm` cookie is present,
  constant-time compare the hash, `_SESSIONset(rqst,"username",…)`, rotate the validator. Logout deletes the
  row. *If `AmmServer_Login` cannot be invoked without a password, set the same session keys it sets
  (`username`,`uid`) — check `userAccountsWeb.c` first.*
- [x] **4.6 Profiles** `profiles.db`: `username admin colour glyph gamesPlayed wins losses territoriesTaken
  armiesKilled armiesLost created lastSeen`. Loaded at start, rewritten atomically on change. Stats are updated
  by the engine's end-of-game hook (step 5.13) — never by the client.
- [x] **4.7 Tests** (`tests/http_smoke.sh`, curl + cookie jars): register without code → refused; with code →
  ok; reuse code → refused; POST without CSRF → refused; restart server → `/api/me` still returns the user.

**Exit check C (passed):** `tests/http_smoke.sh` is green, including the restart case. It uses a temporary
users database and data directory (`FRISK_USERS_DB`, `FRISK_DATA_DIR`) and covers bootstrap admin, invite
required/used/reuse, CSRF rejection, static shell, map discovery, and remember-me after a restart.

---

## 5. Phase D — Core rules engine

All of this is pure C in `engine/`, driven by one entry point:

```c
int game_apply(struct Game *g, int player, const struct Action *a, struct Event *out, int *outCount, char *err);
```

It validates, mutates, appends events, bumps `g->version`, and returns 0 with a reason string on illegal input.
The server never mutates `struct Game` by any other route.

- [x] **5.1 `struct Game`**: id, name, map pack name, password hash, host, status (`OPEN/RUNNING/FINISHED`),
  version, seats[4] {username, faction, alive, hand[], adventureHand[], leaders, missionsDone, stats},
  territories[] {owner, armies, leaderOf}, decks + discard piles, phase, current player, turn number,
  per-turn flags (conqueredThisTurn, cardsTradedThisTurn, pending conquest move {from,to,min,max}),
  created/updated timestamps, last-seen-version per seat (for replay), event log (ring + on-disk tail).
- [x] **5.2 Phases** (enum, one switch): `LOBBY → SETUP_CLAIM → SETUP_PLACE → SETUP_LEADERS →
  [REINFORCE → ATTACK ⇄ CONQUEST_MOVE → FORTIFY → DRAW]* → FINISHED`.
- [x] **5.3 Setup**: seat order shuffled; two start modes chosen at game creation —
  *deal* (territories dealt randomly, 1 army each — recommended for async, removes ~40 waiting turns) or
  *draft* (players claim one territory per turn). Then `SETUP_PLACE`: remaining armies placed in batches
  (default 5 per turn; batch size is a create-game option so async games don't crawl). 2-player games add a
  passive neutral faction per `setup["2"].neutral`.
- [x] **5.4 Reinforcement count**: `max(minReinforce, owned / territoriesPerArmy)` + region bonuses for fully
  owned regions + `strongholdReinforce` per owned stronghold + traded-set bonus. Returned itemised so the UI
  can show the breakdown.
- [x] **5.5 Territory cards**: trade sets (three alike / one of each, wilds substitute), bonus from
  `rules.setBonus`; +2 armies on a traded card's territory if owned; forced trade at ≥ 5 cards at start of
  reinforce; hands of an eliminated player go to the conqueror, who must trade immediately down to < 5.
- [x] **5.6 Attack**: `ATTACK{from,to,dice}` — from owned with ≥ 2 armies, to adjacent enemy, attacker dice
  1–3 (≤ armies−1). **Defender dice are chosen automatically** (max allowed: 2 if armies ≥ 2 else 1) so nobody
  waits on an absent defender. Server rolls with `dice.c`; compare sorted pairs, ties to defender. Event records
  every die, modifiers and losses.
- [x] **5.7 Blitz**: `ATTACK` accepts `repeat:true` to loop until the territory falls or attacker has
  `stopAt` armies left; the whole sequence is one action / one save / one event with all rolls listed.
  A single blitz is capped at 32 battle rounds (`GAME_MAX_BLITZ_ROUNDS`); the event is marked `truncated`
  and the client can issue another ATTACK.
- [x] **5.8 Conquest**: defender at 0 → ownership changes, phase `CONQUEST_MOVE` with min = dice used,
  max = armies−1; `MOVE_IN{count}` returns to `ATTACK`. Sets `conqueredThisTurn`.
- [x] **5.9 Fortify**: one move per turn along a path of connected owned territories (BFS), leaving ≥ 1 army.
  Optional leader move rides with it.
- [x] **5.10 Draw + end turn**: if `conqueredThisTurn` draw one territory card (reshuffle discard when empty);
  advance to next living player; reset per-turn flags; turn counter++.
- [x] **5.11 Elimination & victory**: player with 0 territories is out (cards transfer, leaders removed);
  one player (ignoring neutral) left → `FINISHED`, winner recorded.
- [x] **5.12 Resign / kick**: resign converts the player's territories to neutral (armies stay, never
  attack), discards their cards and removes their leaders. **Kick-by-vote implemented in M6**:
  `POST /api/game/act {type:"kick_vote", target}` starts/joins a persisted vote during a normal turn.
  It is only available once the target's profile `last_seen` is at least seven days old
  (`FRISK_KICK_IDLE_SECONDS` overrides the threshold for tests), needs a majority of the other living
  players, and executes the new `ACT_KICK` removal when it passes. Vote state is part of the game file,
  survives restart, is cleared when the target leaves, and is shown in the players panel.
- [x] **5.13 End-of-game hook**: callback filled by the server to update profiles (step 4.6).
- [x] **5.14 Visibility filter**: `game_view(g, viewerSeat, json)` — hides other players' card hands
  (counts only), secret missions and deck order. Spectators get the public view. This is the **only** function
  that serialises state for clients.
- [x] **5.15 Tests**:
  - `test_rules.c`: table-driven legal/illegal action cases per phase (wrong player, wrong phase, non-adjacent,
    too many dice, moving the last army, fortify through enemy land, trading an invalid set…).
  - deterministic dice (`dice_seed`) for exact combat-resolution assertions.
  - `fuzz_games.c`: 10 000 seeded games of random *legal* actions (legal-move enumerator
    `game_legal_actions()` — also reused by the UI hints) asserting invariants after every action: total armies
    ≥ territories, every territory owned, card conservation (decks + hands + discards = constant), phase/player
    consistency, game terminates. Plus 100 000 random *arbitrary* actions asserting no crash and no state change
    on rejection. Build once with `-fsanitize=address,undefined`.

**Implemented:** 233 deterministic rules checks plus a fuzz run of 10 000 games / 100 000 arbitrary
actions, all green under ASan/UBSan (about 55 s on this machine).

**Exit check D (passed):** `tests/run_tests.sh` green under ASan/UBSan.

---

## 6. Phase E — Leaders, strongholds, sites, adventure cards

- [x] **6.1 Strongholds**: defender in a stronghold territory gets +1 to its highest die; owned strongholds
  add `strongholdReinforce` (already counted in 5.4). The dice event carries `defenderBonus`, and the client
  shows it beside the raw dice.
- [x] **6.2 Leaders**: each player places `leadersPerPlayer` during `SETUP_LEADERS`. A leader sits on a
  territory with armies; +1 to the owner's highest die when attacking from or defending that territory,
  stacking with a stronghold and capped by `rules.dieCap` (default 6). It moves with conquering armies via the
  `MOVE_IN` leader flag or during fortify (both have client toggles), is removed when its territory is
  conquered, and a replacement can be bought at reinforce time when the player has none. The cost is
  `rules.leaderCost` (default 3 armies, LOTR pack 3). `RECRUIT_LEADER` adventure cards also place one free.
- [x] **6.3 Sites of power**: a leader moved into a conquered site via `MOVE_IN`, or a leader ending the turn
  on an owned site, draws one adventure card. Per-seat site bitmasks store the once-per-site-per-player rule,
  the masks are serialized, and the map/card UI shows site markers.
- [x] **6.4 Adventure cards — effect table** in `cards.c`:
  `struct Effect { const char *id; int (*canPlay)(…); int (*apply)(…); enum Timing timing; }`.
  Initial effect ids (pack cards reference these with `args`):

  | Kind | Effect id | Behaviour |
  |---|---|---|
  | mission | `HOLD_TERRITORIES` | hold N listed territories at start of your turn → reward armies |
  | mission | `HOLD_REGION` | own a named region → reward |
  | mission | `CONQUER_N_THIS_TURN` | conquer N territories in one turn → reward |
  | mission | `LEADER_REACH` | bring a leader to a named territory → reward |
  | mission | `HOLD_STRONGHOLDS` | own N strongholds → reward |
  | event | `REGION_ATTRITION` | every player loses 1 army per territory in region (min 1 stays) |
  | event | `REINFORCE_ALL` | each player +N armies on a stronghold they own |
  | event | `LOSE_LEADER_UNLESS` | drawer discards a territory card or loses a leader |
  | event | `SEA_CLOSED` | named link unusable until drawer's next turn |
  | power | `ATTACK_BONUS` | +1 to one attack die for one battle |
  | power | `DEFENSE_BONUS` | played automatically by pre-set rule (see below) |
  | power | `REROLL` | re-roll own dice once in a battle just rolled |
  | power | `EXTRA_REINFORCE` | +N armies this reinforce phase |
  | power | `FREE_FORTIFY` | a second fortify move this turn |
  | power | `RECRUIT_LEADER` | place a leader for free |
  | power | `CANCEL_EVENT` | negate an event just drawn by anyone |

  Events resolve immediately on draw. Missions stay face-down in hand and are checked automatically at the
  owner's start of turn, after each conquest for `CONQUER_N_THIS_TURN`, and after leader moves for
  `LEADER_REACH`. Powers are played with `PLAY_CARD`; card args are read from `map.json`. The sea-link
  effects also make the engine use open map links for attacks and fortify paths, which closes the gap left in
  M2.
- [x] **6.5 Async-safe reactions**: `DEFENSE_BONUS`, `REROLL` and `CANCEL_EVENT` use per-card standing
  orders stored in the seat (never / attack on a stronghold / attack with at least N dice, or never / any
  event for cancellation). The engine consumes the card automatically during the battle or event. The
  standing orders survive serialization and are set from the adventure drawer. Manual play of these reactive
  cards is intentionally rejected in favour of the standing order.
- [x] **6.6 Hand limit**: the hand limit is four; drawing beyond four sets a discard flag, blocks all other
  actions, and the card drawer shows Discard buttons. `DISCARD_ADVENTURE{card}` is the hand slot and is
  persisted. Ending a turn with a site draw over the limit pauses the end-turn action until the discard is
  resolved, then the same End turn action completes.
- [x] **6.7 Tests** `test_cards.c`: 87 checks cover every effect id, leader/stronghold/power modifier
  stacking, the mission timing paths, reactive standing orders, site draw-once, hand limit and sea closure.
  `fuzz_games.c` now carries a 16-card adventure deck, seeds hands and standing orders, plays playable cards
  in legal games, and includes the new action types in arbitrary fuzzing; the full fuzz passes with card
  conservation intact.

**Exit check E (passed):** all engine tests are green (254 rules, 43 serialize, 87 card and 53 map checks);
`mapcheck` confirms every adventure card in the default pack maps to an existing effect, and the full fuzzed
suite (10,000 legal games plus arbitrary actions) passes with adventure card conservation.

---

## 7. Phase F — Persistence

- [x] **7.1 `json.c`**: writer (append with bounds) + a small recursive-descent reader sufficient for our own
  files and `map.json` (objects, arrays, strings with escapes, integers, booleans). Reject anything else.
  **Implemented:** `json_parse` / `json_write_compact`; floats, `null` and `\u` escapes are rejected.
- [x] **7.2 `serialize.c`**: `game_to_json` / `game_from_json`, with a `"format":1` field for future
  migrations. Unknown fields ignored, missing required fields → load failure (game is skipped and reported,
  never half-loaded). **Implemented**, including card/leader conservation and pending-conquest checks.
- [x] **7.3 Atomic save** in `store.c`: write `game_<id>.json.tmp`, `fsync`, `rename`, then `fsync` the
  directory. Called after **every** accepted `game_apply` while still holding `friskLock` (files are a few KB;
  the cost is negligible at this scale and it guarantees no accepted move is ever lost).
  **Implemented:** `store_write_atomic`, called from `games_save` after every action; the HTTP smoke test
  verifies a placement survives `kill -9`.
- [x] **7.4 Event log**: every accepted event carries the game version and is appended as one JSON line to
  `db/frisk/games/game_<id>.log`; the startup/seed events are written when the game starts. The game file
  keeps the last `GAME_LOG_RING` (64) events for fast state loads, which is a deliberate memory bound
  instead of the planned 2 000-event file cap; the append-only file has the full history.
  `/api/game/log?g=&after=V&limit=N` serves the last N events newer than version V from that file and falls
  back to the ring for older games. `seen` updates the per-seat replay watermark.
- [x] **7.5 Startup load**: scan `db/frisk/games/`, load all, rebuild in-memory index, next-id counter =
  max id + 1. A stray `.tmp` is deleted. A corrupt file is renamed `*.corrupt` and logged, startup continues.
  **Implemented** in `games_init`/`load_games`.
- [x] **7.6 Finished games**: kept on disk and listed in profile history; unloaded from RAM after 24 h.
  **Implemented (2026-11-17):** the end-of-game hook appends one JSON line per game to
  `db/frisk/history.log` (id, name, map, finish time, turns, winner, per-player result and battle stats);
  `/api/profile` returns the user's last 20 entries and the lobby Account panel lists them. Finished games
  are freed from their RAM slot 24 h after their last save (`FRISK_FINISHED_TTL_SECONDS` overrides this for
  tests) by a sweep on lobby polls and game creation; stale finished games are not loaded at startup, and
  older finished games without a history line are backfilled once at load. Files and logs stay on disk; an
  unloaded game is no longer openable by URL. This also stops finished games from filling the 64 slots.
  `tests/history_test.py` covers it.
- [x] **7.7 Chat persistence**: append-only `chat/<room>.log`, one JSON line per message. **Implemented**
  in M8 as `{id, t, user, kind:"text|clip|taunt|system", ...}` (text is `text`, clips carry `file/dur/size`,
  taunts carry `index`); the in-memory last-id cache is rebuilt by scanning the file when a room is first
  used, and `/api/chat?after=` reads the durable tail. The planned 200-line RAM cache was unnecessary at LAN
  scale; chats survive restarts.
- [x] **7.8 Tests** `test_serialize.c`: for 1 000 fuzzed mid-game states `save → load → save` is
  byte-identical; truncated/garbled files are rejected cleanly. Manual: `kill -9` during a blitz loop from
  `http_smoke.sh`, restart, state equals the last acknowledged response.
  **Implemented:** `tests/test_serialize.c` covers the valid round-trip, truncated-prefix rejection,
  syntax corruption and unknown-field tolerance; `fuzz_games.c` performs the 1 000 save/load/save checks on
  real fuzzed states. The `kill -9` check needs the HTTP server and moves to M5.

**Exit check F (engine half passed):** round-trip and corrupt-file tests green. The kill-and-restart check
passed in M5: `http_smoke.sh` places armies, kills the server with `kill -9`, restarts it and verifies the
game version, log text and state came back from disk.

---

## 8. Phase G — Client (single page, desktop + mobile)

No frameworks, no build step. ES modules, one CSS file. All user-supplied strings go in via `textContent`.

- [x] **8.1 Shell `index.html` + `app.js`**: hash router (`#/login`, `#/lobby`, `#/game/<id>`, `#/profile/<u>`),
  `api.js` wrapper (`get`, `post` with CSRF auto-attached, JSON errors → toast, `err:"login"` → login view).
  **Implemented** as one dependency-free `res/js/app.js`: `#/login`, `#/lobby` and deep-linkable `#/game/<id>`;
  the profile editor is the Account panel in the lobby rather than a separate `#/profile/<u>` view. API GETs
  add a cache-busting nonce and `cache:"no-store"` because AmmarServer marks every response `max-age=3600`.
- [x] **8.2 Login / register view**: username, password, invite code (register only), clear error messages.
- [x] **8.3 Lobby view**: three lists — *Your games* (with **YOUR TURN** badge, sorted first), *Open games*
  (join), *Running* (spectate); *New game* dialog (name, map pack, seats 2–4, start mode deal/draft, placement
  batch size, optional password); lobby chat panel; invite-code panel; link to profile. Polls
  `/api/lobby?since=V` every 5 s (30 s when the tab is hidden, via `visibilitychange`).
  **Implemented**, including the lobby chat panel (M8); invites and the profile editor are inline panels.
- [ ] **8.4 Pre-game room**: seats, faction/colour pick, ready state, host *Start* (needs ≥ 2), leave.
  **Partial:** seats, host Start (disabled below two players), leave and faction choice are implemented.
  The room lists the pack's factions; a visitor joins as a free faction, and a seated player switches by
  posting `faction` to `/api/game/join` again (no new endpoint; taken factions return `faction`/409). The
  lobby's quick Join still takes the first free faction. An explicit ready state is still deferred.
- [x] **8.5 Map renderer `map.js`**: fetch `board.svg`, inline it, attach one click/tap handler per `#t<N>`;
  fill = owner colour (with a pattern overlay per faction so colour-blind players can tell them apart);
  army counter badge at `label`; leader / stronghold / site glyphs; sea-link lines; highlight classes
  `selectable`, `source`, `target`, `lastAction`. Pan + zoom: wheel/drag on desktop, pinch/drag on touch
  (pointer events, CSS transform on a wrapper `<g>`), double-tap to zoom to territory, *fit* button.
  **Implemented** in `app.js` with dynamic board dimensions from `map.json`, pointer pan/pinch/wheel zoom,
  fit, owner fills, badges, leader/stronghold/site markers and the selectable/source/target classes. The
  per-faction pattern overlays landed in M9; double-tap/double-click zoom to a territory and a dashed
  `last-action` highlight on the latest spatial log event landed on 2026-11-17.
  **Fixed (2026-11-17, owner report "placing armies did not work"):** the map called `setPointerCapture`
  on every `pointerdown`, so the `click` ending a plain mouse press was retargeted to the container and no
  territory ever received a mouse click (claim/place/attack/fortify were all dead with a mouse; touch
  happened to work in Chrome). Capture now starts only when a press becomes a drag (> 6 px) or a pinch.
  Earlier verification used synthetic `element.click()` calls, which bypass capture; the new
  `tests/browser_input.py` drives Chrome with trusted mouse/touch/keyboard events and fails on the old code.
  The LOTR anchor hexes are only 12–17 px on a fitted board, so each territory also gets an invisible hit
  area (its nearest-label Voronoi cell, capped at 0.7 × the mean cell radius), a hover outline and a native
  name tooltip; clickable badges get a pulsing white ring (cyan = chosen source, orange = target; colours
  no faction uses, non-scaling stroke). Before a source is chosen only valid sources are highlighted, then
  only that source's targets. The dice overlay no longer swallows the next map click
  (`pointer-events: none`; any map press dismisses it).
  **Pieces (2026-11-17, owner request):** the pack's 2.6 piece art is now rendered. Each army is drawn
  beside its number as a stack of `unit5`/`unit3`/`unit1` tokens (greedy, at most five, then `+`) tinted
  with the owner's faction colour through `currentColor`; leaders use the `leader` sprite and
  strongholds/sites `pieces/stronghold.svg` / `pieces/site.svg` (text glyphs if a pack lacks them). The
  sprites are copied once into the board's `<defs>` as `<symbol>`s (shape elements only) and drawn with
  `<use>`; badges are layered rings → pieces → numbers so dense clusters never hide a number. Hovering
  (mouse) or tapping (touch) a territory shows an info chip: name, region and bonus, owner and armies,
  leader, stronghold and site. The Players tab lists every region with its bonus and its holder (or how
  much of it you hold).
- [x] **8.6 Turn UI `game.js`**: a small client state machine mirroring the server phase; legal targets come
  from the server (`state.legal`, produced by `game_legal_actions()`), so the client never re-implements rules.
  - Reinforce: breakdown tooltip, tap territory → stepper (+1/+5/all), *Trade cards* shortcut, *Done*.
  - Attack: tap source → tap target → dice count chooser + *Attack* / *Blitz (stop at N)* → dice animation →
    conquest move slider (+ "bring leader" toggle).
  - Fortify: source → reachable destinations highlighted → slider → *End turn*.
  - Confirmation only for irreversible, non-obvious actions (resign, end turn with unplaced armies).
  **Implemented** for setup claim/place/leaders, reinforce (trade shortcut and Done), attack (dice chooser
  and blitz), conquest move, fortify, end turn and resign. Placement, conquest moves and fortifies use a
  slider with a *Place N* / *Move N* confirm button (Enter) plus quick buttons (1 / 5 for placement, *Min*
  for conquest, 1 for fortify); *Blitz* has a *stop at 1/2/3/5/10* choice. Moving a leader with armies is
  a *Bring the leader* toggle on conquest moves and fortifies; attack dice are animated (see 8.7). Resign
  is confirmed and lives in the game header. A scripted run that plays every phase through the real UI
  (trusted mouse and touch input, draft claim through card trades, blitz and fortify) completes with no
  failed step.
- [x] **8.7 Dice `dice.js`**: CSS 3D or sprite roll ~600 ms with `dice_1.wav`, then show server results,
  modifiers as `+1` chips, paired comparison lines, casualty counters. Skippable; user setting to disable
  animation/sound (stored in `localStorage`).
  **Implemented (2026-11-17)** in `app.js` rather than a separate `dice.js`: a map overlay shows every new
  attack seen by polling (your own and other players'; older rolls belong to the replay). CSS pip dice
  tumble for 600 ms with `dice_1.wav`, then show the server's final roll with `+N` bonus chips. Compared
  pairs get won/lost rings, unpaired dice are dimmed, and the casualty line totals blitz rounds and notes a
  conquest. Click dismisses; it auto-hides after 3–4.5 s. *Animate dice* and *Sound effects* checkboxes in
  the Account panel are stored in `localStorage`; `prefers-reduced-motion` skips the tumble.
- [x] **8.8 Polling**: `/api/game/state?g=&since=V` — 1 s when it is someone else's turn and the tab is
  visible, 4 s on your own turn (nothing changes except chat), 20 s when hidden. Responses carry `chatN` so the
  chat tail is fetched only when it moved. Tab title becomes `(!) Your turn — Frisk`; optional
  `Notification` if permission was granted (works on localhost/HTTPS only — degrade silently).
  **Implemented** with `since`/`same` at 1 s visible / 20 s hidden, lobby at 5 s visible / 30 s hidden,
  tab-title turn notification and opt-in desktop `Notification`s (permission button in the account panel;
  browser/secure-context restrictions are surfaced). M8 adds chat polling at 2 s in a game and 3 s in the
  lobby when visible (20 s hidden); the separate `/api/chat` tail leaves the game-state response unchanged.
  Because notifications need a secure context, a short Web Audio chime (no sound file; *Sound effects*
  setting) now plays when a game turns to you, both in the game view and in the lobby.
- [x] **8.9 Cards `cards.js`**: the territory-card hand keeps the auto-suggested set shortcut; the adventure
  drawer shows kind/title/text, a Play button only for `state.legal.playableAdventure`, standing-order
  selectors for reactive powers, Discard buttons under the hand limit, deck/discard counts, and live mission
  progress computed from map metadata (`held`, region, conquest count, leader reach, strongholds).
- [x] **8.10 Replay `replay.js`** ("while you were away"): on entering a game where
  `state.version > seat.lastSeenVersion`, fetch `/api/game/log?after=…&limit=100` and play events back on the
  map at ~1 event/400 ms (attacks highlight source→target, dice losses appear in the text), with *Skip* and a
  1×/2×/4× speed toggle; then POST `seen` so the server stores `lastSeenVersion`. The same replay events and
  the event ring feed the side-panel log. Leaving the game or hiding the tab advances the watermark so only
  genuinely missed events replay.
- [x] **8.11 Layout / responsive CSS**:
  - ≥ 1000 px: map left (flex 1), 340 px right panel with tabs *Players · Log · Chat · Cards*, phase bar on top.
  - < 1000 px: map full-viewport; top strip (phase, current player, armies to place); bottom action bar with
    large (≥ 44 px) buttons; *Players / Log / Chat / Cards* as slide-up sheets with unread badges.
  - `meta viewport`, `touch-action: none` on the map only, safe-area insets, no hover-only affordances,
    landscape + portrait both usable. Dark wood theme reusing `textures/environment/wood.jpg`.
  **Implemented (2026-11-17):** in a game the page is exactly one viewport tall (no page scroll). Wide
  screens: map left, 340 px column with the phase bar + actions on top and *Players · Cards · Log · Chat*
  tabs (Cards also holds the adventure drawer; the last tab is remembered). Portrait below 1000 px: map
  fills the screen, actions + tabs dock at the bottom with ≥ 44 px targets and safe-area padding, and a
  tab opens as a slide-up sheet over the map (tap the tab again, tap the map or press Esc to close).
  Landscape below 1000 px keeps a 290 px side column; at ≤ 500 px height the top bar is hidden in a game.
  Badges show unread log events and chat messages (not your own or system lines), the card count, and
  `!` when a trade/discard is forced. The phone top bar no longer overflows (it made the page 497 px wide
  on a 390 px screen). Lobby faction dots now use each pack's colours instead of grey. Verified by
  screenshots at 1440×900, 1100×720, 820×1180, 390×844 and 844×390 plus a phone interaction check.
- [x] **8.12 End of game**: winner banner (`win.wav` / `lost.wav`), final stats table, *Back to lobby*.
  **Implemented (2026-11-17):** a Victory/Defeat/Game-over banner over the map with a per-player table
  (lands, taken, killed, lost, missions; winner first), *Back to lobby* and *View the board*. The sound
  plays once when the finish is seen live or on first return after an unseen finish, and waits for the
  replay to end.
- [x] **8.13 Offline-asset audit**: `grep -rE "https?://" res/` returns nothing but comments.
  **Passed (2026-11-17):** the only hits in the client and the LOTR pack are SVG/XLink namespace URIs
  (`http://www.w3.org/...`), which are identifiers, not fetches; the old prototype `res/board.html`
  has none either.
- [x] **8.14 Replace old prototypes**: `frisk/board.html` and `frisk/res/board.html` are superseded by
  `res/index.html`; delete them (and `res/textures/map_full.map`) once the new client renders the map.
  **Done (2026-11-17)** on the owner's request. `res/textures/map_full.jpg` (the 2861×4695 board scan) was
  only referenced by the prototypes but is not listed here and was kept.

**Exit check G:** a complete 3-player game played by hand: desktop Firefox + desktop Chromium + a real phone on
the LAN, including one browser being closed for several turns and replaying on return.

---

## 9. Phase H — Chat and voice clips

- [x] **9.1 Upload limit**: the default `DEFAULT_MAX_HTTP_POST_REQUEST_HEADER` /
  `AMMSET_MAX_POST_TRANSACTION_SIZE` is 4 MiB, comfortably above `MAX_CLIP_BYTES = 512 KiB` plus multipart
  overhead, so it is unchanged. The clip handler has `ENABLE_RECEIVING_FILES` and rejects anything above
  512 KiB before writing.
- [x] **9.2 Text chat** `chat.c`: rooms `lobby` and `g<id>`. `POST /api/chat/say {room,text,csrf}` — 1–500
  bytes, UTF-8 validated, control chars stripped, stored raw and JSON-escaped on output
  (`AmmServer_JSONEscape`), rendered with `textContent`. Only seated players post in a game room; spectators
  read. Rate limit 1 msg/s, 20/min per user. System messages (joins, turn changes, eliminations) come from the
  server. **Implemented** with a strict room parser, per-user in-memory rate table, system lines for join,
  leave, start, turn begin, elimination, kick, resign and game over, and `GET /api/chat?room=&after=&limit=`
  for the tail. Taunts share the text rate limit.
- [x] **9.3 Clip upload** `POST /api/chat/clip` (multipart: `room`, `csrf`, `clip`): size ≤ `MAX_CLIP_BYTES`;
  sniff magic bytes and accept only `OggS` (ogg/opus), `1A 45 DF A3` (webm), `RIFF….WAVE` (wav),
  `….ftyp` (m4a/mp4/3gp), `ID3`/MPEG sync (mp3), `#!AMR` (amr); extension derived from the sniff, never from the
  client filename; stored as `<random-token>.<ext>` (token from `AmmServer_GenerateSecureToken` as in
  `Social/media.c`); a `kind:"clip"` chat line is appended. Rate limit 1 clip / 10 s and 30 / day per user.
  **Implemented**; WebM magic is stored as `.weba` (audio-only WebM) so the response is `audio/webm` rather
  than AmmarServer's generic `video/webm` mapping for `.webm`.
- [x] **9.4 Clip serving** `GET /api/clip?f=<name>`: strict name validation (same rule as
  `mediaIsValidName`), login required, stream with `AmmServer_DynamicRequestReturnFile` and the right
  Content-Type. If the Content-Type can't be set for `.webm/.m4a`, add those extensions to AmmServerlib's audio
  table (`stringscanners/audioFiles`, `tools/http_tools.c`) as a small separate change. **Implemented** with
  strict base64url-token + single-dot + lowercase-extension validation and login checks. AmmarServerlib's
  audio scanner now knows `weba`, `m4a` and `amr`; `weba` maps to `audio/webm`, `m4a` to `audio/mp4`.
- [x] **9.5 Client `voice.js`**:
  - Always present: 🎤 button backed by `<input type="file" accept="audio/*" capture>` → on phones opens the
    system recorder, on desktop a file picker → preview `<audio>` → *Send* / *Discard*.
  - When `window.isSecureContext && navigator.mediaDevices`: the same button becomes hold-to-record with
    `MediaRecorder` (prefer `audio/webm;codecs=opus`, fall back to whatever `isTypeSupported` offers), 15 s cap,
    level meter, release to preview.
  - Clips appear in chat as a compact play button with duration; optional *autoplay new clips* setting
    (off by default; browsers block autoplay until the user has interacted anyway).
  **Implemented** in `res/js/voice.js`; file upload is always available (also as an *Upload clip* button in
  secure contexts), hold-to-record uses pointer capture and a 15 s auto-stop, and the preview previews,
  sends or discards the blob.
- [x] **9.6 Preset taunts**: 6–8 short original sounds in `res/sounds/taunts/`, sent as `kind:"taunt"` chat
  lines referencing an index (no upload). **Implemented** with eight synthesized `taunt1..8.mp3` cues and
  taunt buttons in both chat panels; incoming taunts play on arrival (own taunts play immediately).
- [x] **9.7 Housekeeping** via `AmmServer_AddScheduler` (hourly): delete clips of games finished > 30 days ago
  and orphaned files not referenced by any chat log; cap `clips/` at a configurable total size (oldest first).
  **Implemented** in a dedicated hourly `chat.c` thread (`FRISK_CLIP_CAP_MB`, default 256 MB, `0` = no cap)
  because `AmmServer_AddScheduler` currently delegates to a stub `schedulerAdd` that does nothing. Orphans are
  only removed after a one-hour grace period.
- [x] **9.8 Optional HTTPS**: document in `README` how to start with `AmmServer_StartSSL` + a self-signed
  cert to unlock hold-to-record and notifications. Not required for anything else. Documented in
  `AmmarServerGames/README.md`; `-DUSE_OPENSSL=ON` required. A small `UserAccounts/sha256.h` symbol-prefix fix
  lets the self-contained SHA-256 helper coexist with OpenSSL 3's deprecated `SHA256_*` declarations.
- [x] **9.9 Tests** (in `http_smoke.sh`): oversized clip refused; a renamed `.exe`/HTML file refused; path
  traversal in `f=` refused; unauthenticated fetch refused; clip still playable after restart. **Implemented**,
  plus text/taunt/game-room/spectator checks, wav/webm/m4a sniff and content-type checks, byte-for-byte clip
  round-trip, duration, empty-message and rate-limit checks. The smoke server now uses an isolated temporary
  web root (a copy of `index.html` + `maps/`) so `FRISK_CLIPS_DIR` never pollutes the source tree.

**Exit check H:** a clip recorded on a phone over plain HTTP plays on the desktop client; chat history is
intact after a restart.

**M8 verification (2026-11-17):** the HTTP smoke suite uploaded a WAV with a lying `.exe` filename, fetched it
back byte-for-byte with `audio/wav`, repeated the check for WebM (`audio/webm`) and M4A (`audio/mp4`), refused
oversized/renamed/path-traversal/unauthenticated requests, and verified the text/taunt/clip chat and the clip
itself across `kill -9`. A headless Chromium session registered, sent a lobby message through the real form,
uploaded a WAV through `voice.js`, previewed and sent it, received the rendered clip and fetched it, and sent
a taunt, with no console errors. A second headless Chromium run with a fake microphone verified the secure
context path: hold-to-record started `MediaRecorder`, produced a `.weba` clip over localhost, previewed and
posted it, and the chat fetched it as `audio/webm`. A third headless run opened a game room and posted
through the game chat panel.

Deviations recorded for M8:
- Clips live in `res/uploads/clips/`, not `db/frisk/clips/`: AmmarServer's static streaming path refuses to
  serve files outside the configured web root. Names are 16 random bytes (22 base64url characters), the
  `/api/clip` handler requires login and validates the name, but a guessed full token is also served by the
  ordinary static path. This is the same unguessable-URL trade-off Social's uploads already make. The
  `FRISK_CLIPS_DIR` environment variable overrides the directory (the test suite points it at its temporary
  web root).
- The planned `kind:"clip"` line stores `file`, `dur` (milliseconds, client-supplied and clamped) and `size`;
  the client renders duration from that field.
- Housekeeping uses a dedicated thread because the framework's scheduler is a stub, as noted in 9.7.
- WebM is stored as `.weba`; the `.webm` extension is claimed by AmmarServer's video table before the audio
  table can see it.
- The `UserAccounts` SHA-256 helper is symbol-prefixed to build with OpenSSL 3 (HTTPS documentation).

---

## 10. Phase I — Hardening and polish

- [x] **10.1 Authorisation audit**: for every endpoint in 3.4 list who may call it; add a negative test for
  each (not logged in, not seated, not your turn, not host, spectator). **Implemented** as the matrix in
  `frisk/README.md` plus `tests/auth_audit.py`, which exercises anonymous, non-seated, non-host, missing-CSRF,
  invalid-input and spectator cases for every endpoint and the admin surface.
- [x] **10.2 Input audit**: every numeric param range-checked; every string length-checked; no `strcat`
  without bounds (use the `socialAppendChunk` pattern); build with `-Wall -Wextra -fsanitize=address,undefined`
  and run the full smoke script. **Implemented** with strict `frisk_param_uint`/`frisk_post_uint` parsers and
  unbounded-copy rejection in `frisk_param_str`/`frisk_post_str`, optional-parameter checks in the lobby/state/
  log handlers, and the parent SHA symbol fix. Engine tests run under ASan/UBSan by default; the sanitized
  server smoke is recorded below.
- [x] **10.3 Load sanity**: script 20 concurrent polling clients + 4 acting clients for 10 minutes; watch for
  lock contention, descriptor leaks (`ls /proc/<pid>/fd | wc -l` stable) and memory growth. **Implemented** as
  `tests/load_test.py`; a 10-minute run sampled `/proc/<pid>/fd` and RSS. Results are recorded below.
- [x] **10.4 Long-game check**: a fuzz-driven 2 000-turn game through the HTTP layer to confirm log rollover,
  deck reshuffles and file size stay bounded. **Implemented** as `tests/long_game.py` on a generated
  8-territory pack, with seeded random legal choices; results are recorded below.
- [x] **10.5 Admin page** (admin only): list users, revoke invites, delete a game, force-neutralise an
  abandoned seat, storage usage. **Implemented** as `/api/admin/overview` and `/api/admin/neutralise`, with
  `#/admin` UI (users, invites, games/seats, storage, refresh, revoke/delete/neutralise actions). Neutralise
  is available during the normal turn phases; delete covers an abandoned setup.
- [x] **10.6 Accessibility / UX pass**: keyboard shortcuts on desktop (Enter = confirm, Esc = cancel,
  `1/2/3` = dice), focus outlines, colour-blind patterns verified, reduced-motion setting respected.
  **Implemented** with Enter/Esc/1–3 shortcuts, visible focus outlines, per-faction stripe/dot/hatch SVG
  fills (plus neutral dots), and a `prefers-reduced-motion` CSS rule.
- [x] **10.7 Docs**: `frisk/README.md` — build, run, port, data directory, backing up `db/frisk/`, creating the
  first (admin) account, making invites, adding a map pack, enabling HTTPS. **Implemented**, including the
  authorization matrix, admin page, clip-storage caveat, test commands and HTTPS setup.

**M9 verification (2026-11-17):**
- **10.1:** `tests/auth_audit.py` ran every endpoint negative case plus the admin surface and finished
  "All authorization audit tests passed"; it is part of `run_tests.sh`/CTest.  The roles are also written up
  as the matrix in `frisk/README.md`.
- **10.2:** all numeric parameters now go through a strict digit/range parser and string copies reject
  over-length fields; a final `grep` found no unbounded `strcat` in production C.  The standalone engine
  suite ran under ASan/UBSan (257 rules checks, map tests and fuzz green), and a sanitized server build ran
  the complete HTTP smoke suite with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
  `UBSAN_OPTIONS=halt_on_error=1` with no sanitizer reports.
- **10.3:** the 10-minute run used 20 polling clients and 4 acting clients: 13,323 polls, 0 poll failures,
  63 actions, 0 action failures, file descriptors 22 final / 24 peak, RSS 15,180 -> 17,064 KiB.  A final
  31-second check on the final binary repeated the 20+4 shape (674 polls, 0 failures, descriptors 23 flat,
  RSS +4 KiB).
- **10.4:** seeded fuzz choices drove 2,001 turns through the HTTP API on the generated 8-territory pack:
  10,765 actions, 35 card draws (territory deck reshuffled), 11 trades, 14,371-byte serialized game file,
  1,840,025-byte durable log, 210.9 seconds, game still running.
- **10.5:** the admin API and `#/admin` page were exercised in headless Chromium: user/storage rendering,
  game listing and deletion all passed; `auth_audit.py` also covers admin-only access and the
  setup-phase neutralise refusal plus a positive running-phase neutralise.
- **10.6:** headless Chromium started a game by pressing Enter on the marked confirm control and verified
  the `friskPat0..3`/`friskPatN` SVG patterns and pattern fills; focus-visible outlines and the
  `prefers-reduced-motion` rule are in the CSS.
- **10.7:** `frisk/README.md` now documents build/run, environment variables, data layout and backups,
  first-admin/invites, map packs, HTTPS, admin, tests, the authorization matrix and the keyboard shortcuts.

Deviations/notes: admin neutralise reuses the kick action and is available in the reinforce/attack/fortify
phases; an abandoned setup is deleted from the admin page.  The load and long-game tests use a generated
tiny pack so they never touch `res/maps/lotr` or the live database.  A failed `game_start` now validates
starting armies before mutating state (and restores the open/lobby status on the remaining error paths),
which fixed a partial `RUNNING`/`LOBBY` state found while building the stress pack.

---

## 10b. Phase J — Printed adventure cards (M10, owner decision 2026-11-17)

The owner asked that the cards "trigger correctly" (e.g. *Appoint a Second Leader*, *Borne to Another
Place*, *The Way Is Under the Mountains*) and chose **printed cards, no Fellowship**: the LOTR pack's
adventure deck follows the scanned cards in `res/textures/cards/` instead of §0's "generic adventure
effects". The game is still last-player-standing (card points are shown, not scored) and the ten
Fellowship cards stay out (there is no Ring track). The generic effects stay in the engine for other packs.

- [x] **10b.1 Card flow from the rulebook** (card section only): deal 4 non-event adventure cards to every
  player at the start; at the end of a turn in which one of your Leaders conquered a territory with a Site
  of Power, draw 1 card; Event cards ("Play immediately") resolve at once and the drawing continues until
  a Mission or Power card is drawn; more than 4 cards in hand → discard one of your choice. A Leader
  "conquers" a site when it moves in with the conquest. (Replaces §6's leader-on-site end-of-turn draw.)
- [x] **10b.2 Board data**: faction sides (Elves, Rohan = good; Mordor, Isengard = evil), ports (ends of
  sea-lines), bridges (river crossings drawn on the board) and mountain ranges (territories next to each
  range), read off the board scan; plus a re-check of every badge anchor against the printed territory.
- [x] **10b.3 Deck (50 cards)**: 13 events (4× Appoint a Second Leader, Aragorn Arrives, The Black Gates
  Open, The Entmoot, They've Brought a Cavetroll, Corsairs of Umbar, Local Uprising, Raiders, The
  Mustering of Middle-earth, Winter Storms), 15 powers (Ambush!, Black Sails, Borne to Another Place,
  Courage Alone Will Not Save You!, 2× Gríma Wormtongue, Move by Night, Siege Machines, Strategic
  Withdrawal, The Enemy Is at Hand!, 2× The Way Is Under the Mountains, Violent Storms, 2× You Shall Not
  Pass!) and the 22 site Missions (good/evil rewards: battalions in a region, territory, any territory or
  port; an extra territory card; Erech's 6 Oathbreakers).
- [x] **10b.4 Engine**: invasion tracking (an invasion = attacks from one territory into one territory
  until it falls or the attacker switches), the new effects, mission claims with reward placement,
  pending choices that pause the end of the turn (a free leader from an event), standing orders for the
  defender's cards (Courage Alone, Strategic Withdrawal, Violent Storms — defenders are offline in async
  play), a state snapshot so a failed action never leaves partial changes, and save-file migration
  (games saved with the old 36-card deck are rebuilt with the new deck while no adventure card is out).
- [x] **10b.5 Server and client**: action parameters, legal plays per card, a Play flow per card (pick a
  territory, an attack pair, a leader and destination, or a bridge), mission claim and reward placement,
  standing-order selectors, and markers for active effects (frozen territories, blocked bridges, Corsairs).
- [x] **10b.6 Tests**: engine tests per printed effect, fuzzing with the new effects (card conservation
  including Local Uprising cards), and a trusted-input browser run that plays every card type.

**Exit check J:** every printed card's effect is covered by an engine test and the browser run plays each
power, claims missions and resolves events through the real UI.

**M10 verification (2026-11-17):** `tests/test_printed.c` (129 checks) covers every printed effect, the
rulebook deal and draw chain, the free-Leader and mission-reward pauses, the defenders' standing orders,
the Oathbreakers and save round-trips. `frisk_fuzz` alternates generic and printed-deck games; 4,000 games
(5.3 M actions) passed under ASan/UBSan. `tests/browser_cards.py` (in `run_tests.sh`) plays, with trusted
mouse input, Borne to Another Place, You Shall Not Pass!, a site mission and its battalions, Ambush!,
Gríma Wormtongue, Siege Machines, Black Sails, The Way Is Under the Mountains and Move by Night, sees the
defender's Courage Alone standing order fire, and ends the turn into Appoint a Second Leader (event
drawn → free Leader placed on the map). The other events, and Strategic Withdrawal and Violent Storms
(standing orders through the same path as Courage Alone), are covered by the engine tests only. A copy
of the live `game_1.json` (old 36-card deck, setup phase) loads and is rebuilt with the 50-card deck and
4 cards dealt to each player.

Deviations and findings recorded during M10:

- **Board corrections** read off the board scan: 31 badge anchors moved, the site list now matches the
  mission cards (Fornost, Forodwaith, Brown Lands, Gap of Rohan, Eregion, Khand, Minhiriath, …),
  Isengard's stronghold is Fangorn (not Gap of Rohan), 8 missing borders added and 12 wrong ones removed
  (list in `tools/mkmap.py` `ADJ_ADD`/`ADJ_REMOVE`).
- **Borders left unchanged pending the owner** (unclear on the scan): Eastern Angmar–Withered Heath
  (possibly missing), Eastern Angmar–Rhudaur, Mount Doom–Nurn, Barad-Dûr–Nurn, Nurn–Near Harad, North
  Rhûn–Rhûn Hills, Khand–South Rhûn, Borderlands–Old Forest, The Shire–North Downs / Evendim Hills,
  Lothlórien–Fangorn; and the Lune Valley–Evendim Hills bridge.
- **Fortify across a blocked bridge** is allowed only with the Leader moving along (the legal list marks
  it `needsLeader`), following "only a Leader can lead battalions across".
- **Fuzz findings fixed:** effects that last "until your next turn" (Winter Storms, Corsairs, You Shall
  Not Pass!) now end when their owner resigns or is eliminated (a frozen territory never thawed); the
  rulebook draw stops when only events are left to draw, and the Local Uprising draw stops when only
  wild territory cards are left (both looped forever). Printed-deck fuzz games may run 100,000 actions.
- `frisk_fuzz` takes a third argument, the first game number, to replay a reported failure.

**Tooling (owner request 2026-11-17):** `tools/frisk_edit.py` downloads the resource archive from
`http://ammar.gr/frisk_resources.zip` and runs a local browser editor for a pack's polygons, label anchors,
borders, sea-lines, bridges, mountain ranges, territory-card symbols and adventure cards. The owner chose
`map.json` as the source of truth (mkmap.py is a bootstrap that needs `--force`). Saves are validated by
`mapcheck`, backed up to `frisk/map_backups/`, and may not add, remove or reorder territories or cards
(saved games index them). `tests/map_editor_test.py` (in `run_tests.sh`) covers the download, the save
checks (an unchanged save is byte-identical) and the editor with trusted mouse input.

---

## 11. Build order and milestones

| # | Milestone | Phases | Demonstrable result |
|---|---|---|---|
| M1 | Board is data | A | `mapcheck` passes, SVG map renders |
| M2 | Rules proven | D | fuzz + unit tests green under sanitizers |
| M3 | Durable state | F (engine half) | round-trip + corrupt-file tests green |
| M4 | Server up | B, C | invite-only register/login survives restart |
| M5 | Playable core | lobby (3.4), G 8.1–8.8, F 7.3–7.5 | full classic game across two browsers, survives `kill -9` |
| M6 | Async comfort | 5.12, 8.10, 8.8 notifications | "while you were away" replay, idle-kick vote |
| M7 | Full rules | E, 8.9 | leaders, strongholds, sites, adventure cards in play |
| M8 | Talk | H | chat + voice clips on phone and desktop |
| M9 | Ship | I | audits, load test, README |
| M10 | Printed cards | J | the LOTR adventure cards follow their printed text |

Each milestone ends with its phase exit check; do not start the next one with a red check.

**M5 verification (2026-11-16):** two headless Chromium profiles played a complete game to victory on a
temporary 6-territory test pack with the same client and rules engine, while the 64-territory LOTR pack was
exercised through 1,200 browser-driven actions. Claim, placement, leader placement, reinforcement, card
trades, attack/blitz, conquest moves, fortify, end turn, resign and the finished view were all driven through
the DOM; deep links (`#/game/<id>`) and remember-me recovery across server restarts were verified. The HTTP
smoke test creates/joins/starts a game, plays a placement, kills the server with `kill -9`, restarts it and
checks persistence. Carried into later milestones: faction choice/ready state, colour-blind overlays, dice
animation/sound, chat and 24 h unloading of finished games (7.6); the durable replay log (7.4) landed in M6.

**M6 verification (2026-11-17):** a fresh browser session replayed 40 events from a durable log, honoured
1×/2×/4× and Skip, stored the seen watermark, did not replay again on reload, and replayed a single event
generated while the tab was away. A two-player inactivity vote started in the players panel removed the
target and finished the game with the expected winner; a three-player vote was started, the server was
killed with `kill -9`, and after restart the vote (1/2) and its record were still present, the second vote
completed the removal, and the kicked player's lands became neutral. The HTTP smoke test covers the durable
log, `after=` history, the `seen` watermark and log survival across `kill -9`. Background music was added
outside the plan: the owner's source tracks are converted to `res/music/bg1..bg6.mp3`,
`combat.mp3`, `combat2.mp3`, `combat3.mp3`, `victory.mp3` and `defeat.mp3`, with a topbar on/off control, a
volume slider, a shuffled background playlist, combat music during your attack phase and victory/defeat
themes.

**M7 verification (2026-11-17):** a browser game was loaded with the full adventure hand. The drawer showed
mission progress (`Met: 2/2 held` and the unmet leader mission), played *The Entmoot* (+3 reinforcements), set
and persisted a stronghold defence standing order, and placed a free leader by selecting a highlighted
territory. A later end turn on a site drew a mission, exceeded the hand limit, paused the end turn, showed
Discard buttons, cleared the flag after one discard, and completed the end turn on the second click. An
earlier end turn drew and resolved *Winter Storms* (region attrition) correctly. All 87 card checks, the
full fuzz with seeded adventure cards, the HTTP smoke suite and the parent CTest pass.

Deviations recorded for M7: `LOSE_LEADER_UNLESS` auto-discards a territory card when possible (there is no
async prompt), `REINFORCE_ALL` uses the first stronghold a player owns, and the reactive cards are only
available through standing orders, not manual battle-time plays. The UI is a functional card drawer rather
than a full-screen card detail sheet.

---

## 12. Open points to confirm before/while implementing

1. **Shared vs separate user database** — plan assumes shared `db/users.db` (one account across AmmarServer
   services). Switch the `#define` if frisk should have its own.
2. **Port** — plan assumes 8090; change if it collides with another service in the suite.
3. **Leader/stronghold die cap** — plan caps a modified die at 6; alternative is uncapped (a 6 + 1 beats a 6).
4. **Start mode default** — plan defaults to *deal* for async friendliness; *draft* remains selectable.
5. **Idle-kick threshold** — default 7 days of inactivity before a kick vote is allowed.
