#!/usr/bin/env python3
"""Play the printed LOTR adventure cards through the real UI (PLAN.md 10b).

Starts an isolated server with the client and the LOTR pack, deals a game,
then edits the saved game (server stopped): seat 0 holds Borne to Another
Place, You Shall Not Pass!, Ambush!, Gríma Wormtongue, Siege Machines, Black
Sails, The Way Is Under the Mountains, Move by Night and a site mission, has
one Leader, a sea-line and a stronghold to invade, and Appoint a Second Leader
on top of the deck with a site conquest recorded (so the end of the turn draws
it); seat 1 holds Courage Alone Will Not Save You! as a standing order.
Headless Chrome then plays the cards with trusted mouse input and the server
state is checked after each.

Skips (exit 0) without Chrome/Chromium, Python `websockets` or the LOTR pack
(res/maps/lotr is not in git).
"""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

import requests

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from browser_input import RESULTS, Abort, Page, check, find_chrome, require  # noqa: E402
from long_game import (FRISK_DIR, act, choose_action, form, get, make_invite,  # noqa: E402
                       register, start_server)

PORT = int(os.environ.get("FRISK_CARDS_PORT", "18196"))
PACK = os.path.join(FRISK_DIR, "res", "maps", "lotr")


def login(base, name):
    session = requests.Session()
    result = form(session, base + "/api/login", {"name": name, "password": "password123"})
    if result.get("ok") != 1:
        raise RuntimeError(f"login {name}: {result}")
    return session, get(session, base + "/api/me")["csrf"]


def build_res(tmp):
    res = os.path.join(tmp, "res")
    os.makedirs(os.path.join(res, "maps"), exist_ok=True)
    for item in ("index.html", "js", "css"):
        source = os.path.join(FRISK_DIR, "res", item)
        (shutil.copytree if os.path.isdir(source) else shutil.copy)(source, os.path.join(res, item))
    shutil.copytree(PACK, os.path.join(res, "maps", "lotr"))


def arrange(path, meta, me):
    """Give seat `me` the cards under test and seat 1-me Courage Alone."""
    index = {c["id"]: i for i, c in enumerate(meta["adventureCards"])}
    names = {t["name"]: i for i, t in enumerate(meta["territories"])}
    other = 1 - me
    g = json.load(open(path))
    terr = g["territories"]
    mine = [i for i, t in enumerate(terr) if t["owner"] == me]
    for i, t in enumerate(terr):
        t["leader"] = 0
        t["armies"] = 9 if t["owner"] == me else 3
    mission = next(c for c in meta["adventureCards"] if c["effect"] == "SITE_MISSION" and
                   names[c["args"]["site"]] in mine and
                   c["args"]["reward"] in ("region", "territory", "any", "port"))
    site = names[mission["args"]["site"]]
    leader = next(t for t in mine if t != site)
    terr[leader]["leader"] = me + 1            # Borne to Another Place moves it to the site
    g["players"][me]["leaders"] = 1
    g["players"][other]["leaders"] = 0
    keep = {site, leader}
    # A sea-line invasion (Black Sails) and a stronghold invasion (Siege
    # Machines); bridge 3 (Rhudaur) gets blocked by You Shall Not Pass!.
    blocked = {meta["bridges"][3]["a"], meta["bridges"][3]["b"]}
    sea = next((l["a"], l["b"]) for l in meta["links"]
               if l["kind"] == "sea" and not {l["a"], l["b"]} & (keep | blocked))
    keep |= set(sea)
    siege = next((n, s) for s, t in enumerate(meta["territories"]) if t["stronghold"]
                 for n in t["adjacent"] if not {n, s} & (keep | blocked))
    for t, owner in ((sea[0], me), (sea[1], other), (siege[0], me), (siege[1], other)):
        terr[t]["owner"] = owner
        terr[t]["neutral"] = False
        terr[t]["armies"] = 9 if owner == me else 3
    mine = [i for i, t in enumerate(terr) if t["owner"] == me]
    for p in g["players"]:
        g["decks"]["adventure"].extend(p["adventureHand"])
        p["adventureHand"], p["adventureAuto"], p["adventureAutoN"] = [], [], []
    for cid in ("p03", "p14", "p01", "p05", "p08", "p02", "p11", "p07", mission["id"]):
        g["decks"]["adventure"].remove(index[cid])
        g["players"][me]["adventureHand"].append(index[cid])
        g["players"][me]["adventureAuto"].append(0)
        g["players"][me]["adventureAutoN"].append(0)
    g["decks"]["adventure"].remove(index["p04"])
    g["players"][other]["adventureHand"] = [index["p04"]]
    g["players"][other]["adventureAuto"] = [2]      # standing order: always
    g["players"][other]["adventureAutoN"] = [1]
    # Appoint a Second Leader on top of the deck, drawn at the end of the turn
    for pile in ("adventure", "adventureDiscard"):
        if index["e01"] in g["decks"][pile]:
            g["decks"][pile].remove(index["e01"])
    g["decks"]["adventure"].append(index["e01"])
    g["printed"]["state"][0] = 1                # a Leader conquered a site
    # The engine's reader takes raw UTF-8 only (no \\u escapes), as it writes.
    with open(path, "w", encoding="utf-8") as out:
        json.dump(g, out, ensure_ascii=False)
    return mine, site, leader, mission, sea, siege


async def run(chrome_bin, websockets):
    tmp = tempfile.mkdtemp(prefix="frisk-cards-")
    server = chrome = None
    meta = json.load(open(os.path.join(PACK, "map.json")))
    try:
        build_res(tmp)
        server, base = start_server(PORT, tmp)
        anna = register(base, "anna")
        bert = register(base, "bert", make_invite(base, anna[0], anna[1]))
        game_id = form(anna[0], base + "/api/game/create", {
            "csrf": anna[1], "name": "Cards", "map": "lotr", "seats": 2,
            "mode": "deal", "batch": 60})["id"]
        form(bert[0], base + "/api/game/join", {"csrf": bert[1], "g": game_id, "faction": 2})
        form(anna[0], base + "/api/game/start", {"csrf": anna[1], "g": game_id})
        state = get(anna[0], base + "/api/game/state", {"g": game_id})
        check(len(state["adventureHand"]) == 4 and
              all(c["kind"] != "event" for c in state["adventureHand"]),
              "the rulebook deal gives 4 non-event adventure cards")
        users = {"anna": anna, "bert": bert}
        for _ in range(3000):
            state = get(anna[0], base + "/api/game/state", {"g": game_id})
            if state["phase"] == "reinforce" and state["players"][state["current"]]["user"] == "anna":
                break
            current = state["players"][state["current"]]["user"]
            own = get(users[current][0], base + "/api/game/state", {"g": game_id})
            chosen = choose_action(own)
            if chosen:
                act(base, users[current][0], users[current][1], game_id, chosen[0], **chosen[1])
        me = state["you"]
        server.terminate()
        server.wait(timeout=10)
        mine, site, leader, mission, sea, siege = arrange(
            os.path.join(tmp, "data", "games", f"game_{game_id}.json"), meta, me)
        server, base = start_server(PORT, tmp)
        session, csrf = login(base, "anna")

        def state_now():
            return get(session, base + "/api/game/state", {"g": game_id})

        loaded = state_now()
        require(loaded.get("phase") == "reinforce", "the arranged game loads")

        profile = os.path.join(tmp, "chrome")
        chrome = subprocess.Popen([chrome_bin, "--headless=new", "--remote-debugging-port=0",
                                   f"--user-data-dir={profile}", "--no-first-run",
                                   "--no-default-browser-check", "--window-size=1300,900",
                                   "about:blank"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        port_file = os.path.join(profile, "DevToolsActivePort")
        for _ in range(200):
            if os.path.exists(port_file) and os.path.getsize(port_file) > 0:
                break
            time.sleep(0.05)
        cdp = int(open(port_file).read().split()[0])
        for _ in range(100):
            pages = [t for t in json.load(urllib.request.urlopen(f"http://127.0.0.1:{cdp}/json/list"))
                     if t["type"] == "page"]
            if pages:
                break
            time.sleep(0.05)
        async with websockets.connect(pages[0]["webSocketDebuggerUrl"], max_size=2 ** 26) as ws:
            page = Page(ws)
            await page.call("Page.enable")
            await page.call("Page.navigate", url=base + "/")
            await page.wait("document.readyState === 'complete'")
            await page.js("""fetch('/api/login', {method: 'POST', body: (() => {
                const f = new FormData(); f.append('name', 'anna');
                f.append('password', 'password123'); return f; })()}).then(r => r.json())""")
            await page.js("localStorage.setItem('friskMusic', 'off')")
            form(session, base + "/api/game/act", {"csrf": csrf, "g": game_id, "type": "seen"})
            await page.call("Page.navigate", url=base + f"/?cards#/game/{game_id}")
            require(await page.wait("document.querySelectorAll('#hit-areas .hit').length > 0 && "
                                    "document.getElementById('replay-bar').classList.contains('hidden')", 15),
                    "the board loads")

            async def click_territory(t):
                x, y = await page.js(f"""(() => {{ const r = document.getElementById('t{t}').getBoundingClientRect();
                    return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")
                await page.click(x, y)
                await asyncio.sleep(0.5)

            async def click_button(container, text, prefix=False):
                match = f"b.textContent.startsWith({json.dumps(text)})" if prefix else \
                    f"b.textContent === {json.dumps(text)}"
                box = await page.js(f"""(() => {{ const b = [...document.querySelectorAll({json.dumps(container + ' button')})]
                    .find(b => {match}); if (!b) return null; b.scrollIntoView({{block: 'center'}});
                    const r = b.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")
                if box is None:
                    return False
                await page.click(*box)
                await asyncio.sleep(0.6)
                return True

            async def tile_button(card_id, label="Play"):
                slot = next(i for i, c in enumerate(state_now()["adventureHand"]) if c["id"] == card_id)
                await page.js("document.querySelector('#side-tabs [data-tab=cards]').click()")
                await asyncio.sleep(0.2)
                box = await page.js(f"""(() => {{ const tile = document.querySelectorAll('#game-adventure .adv-card')[{slot}];
                    const b = [...tile.querySelectorAll('button')].find(b => b.textContent === {json.dumps(label)});
                    if (!b) return null; b.scrollIntoView({{block: 'center'}}); const r = b.getBoundingClientRect();
                    return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")
                if box is None:
                    return False
                await page.click(*box)
                await asyncio.sleep(0.6)
                return True

            # Borne to Another Place: the Leader goes to the mission's site
            require(await tile_button("p03"), "Borne to Another Place has a Play button")
            await click_territory(leader)
            await click_territory(site)
            st = state_now()
            check(st["territories"][site]["leader"] == me + 1 and
                  st["territories"][leader]["leader"] == 0, "Borne to Another Place moved the Leader")

            # You Shall Not Pass!
            slot = next(i for i, c in enumerate(st["adventureHand"]) if c["id"] == "p14")
            await page.js(f"""document.querySelectorAll('#game-adventure .adv-card')[{slot}]
                .querySelector('select').value = '3'""")
            require(await tile_button("p14", "Block"), "You Shall Not Pass! has a bridge picker")
            check([3, me] in state_now()["printed"]["blockedBridges"], "the chosen bridge is blocked")

            st = state_now()
            act(base, session, csrf, game_id, "place", t=mine[0], count=st["legal"]["reinforcementsPending"])
            act(base, session, csrf, game_id, "done_reinforce")
            await asyncio.sleep(1.5)

            # A site mission and its battalions
            require(await tile_button(mission["id"], "Complete mission"),
                    f"{mission['title']} can be completed with a Leader on its site")
            st = state_now()
            require(st["printed"]["bonus"] > 0, "the mission reward waits to be placed")
            target = st["legal"]["bonusPlaceable"][0]
            before = st["territories"][target]["armies"]
            await click_territory(target)
            require(await click_button("#game-actions", "Place", prefix=True), "mission battalions can be placed")
            st = state_now()
            check(st["printed"]["bonus"] == 0 and st["territories"][target]["armies"] > before,
                  "the mission battalions are on the board")

            def finish_conquest():
                st = state_now()
                if st["phase"] == "conquest_move":
                    act(base, session, csrf, game_id, "move_in", **{
                        "from": st["pending"]["from"], "to": st["pending"]["to"], "count": st["pending"]["min"]})

            async def select(pair_from, pair_to):
                await click_territory(pair_from)
                await click_territory(pair_to)

            # Ambush! and Gríma on a selected invasion; the defender's Courage Alone answers
            # (2-player games have neutral territories: invade one of bert's)
            pair = next(a for a in st["legal"]["attacks"]
                        if st["territories"][a["to"]]["owner"] == 1 - me and
                        not {a["from"], a["to"]} & {*sea, *siege})
            await select(pair["from"], pair["to"])
            before = st["territories"][pair["from"]]["armies"]
            require(await click_button("#game-actions", "Play Ambush!"), "Ambush! is offered for the invasion")
            check(state_now()["territories"][pair["from"]]["armies"] == before + 3, "Ambush! added 3 battalions")
            await select(pair["from"], pair["to"])
            require(await click_button("#game-actions", "Play Gríma Wormtongue"), "Gríma is offered for the invasion")
            st = state_now()
            check(st["territories"][pair["to"]]["armies"] == 1 and
                  st["territories"][pair["from"]]["armies"] == before + 5,
                  "Gríma moved 2 enemy battalions to the invading territory")
            await select(pair["from"], pair["to"])
            require(await click_button("#game-actions", "1 die"), "the invasion can be attacked")
            log = [e["text"] for e in state_now()["log"]]
            check(any("Courage Alone" in line for line in log),
                  "the defender's Courage Alone standing order fired" +
                  ("" if any("Courage Alone" in line for line in log) else ": " + " | ".join(log[-8:])))

            # Siege Machines on a stronghold
            finish_conquest()
            await select(*siege)
            require(await click_button("#game-actions", "Play Siege Machines"),
                    f"Siege Machines is offered against {meta['territories'][siege[1]]['name']}")
            invasion = state_now()["printed"]["invasion"] or {}
            check(invasion.get("noStronghold") is True, "the stronghold gives no defence bonus")
            await select(*siege)
            check(await click_button("#game-actions", "1 die"), "the stronghold can be attacked")

            # Black Sails on a sea-line
            finish_conquest()
            await select(*sea)
            require(await click_button("#game-actions", "Play Black Sails"), "Black Sails is offered over the sea-line")
            invasion = state_now()["printed"]["invasion"] or {}
            check(invasion.get("bonus") == 1, "the seaborne invasion gets +1 on its highest die")
            await select(*sea)
            check(await click_button("#game-actions", "1 die"), "the sea-line can be crossed")

            # The Way Is Under the Mountains
            finish_conquest()
            st = state_now()
            pick = next(((a, b) for r in meta["mountains"] for a in r["territories"] for b in r["territories"]
                         if st["territories"][a]["owner"] == me and st["territories"][a]["armies"] >= 2 and
                         st["territories"][b]["owner"] == 1 - me and
                         b not in meta["territories"][a]["adjacent"]), None)
            require(pick is not None, "a mountain-range invasion is available")
            require(await tile_button("p11"), "The Way Is Under the Mountains has a Play button")
            await click_territory(pick[0])
            await click_territory(pick[1])
            check(any(a["from"] == pick[0] and a["to"] == pick[1] for a in state_now()["legal"]["attacks"]),
                  "the invasion under the mountains is open")
            check(await click_button("#game-actions", "1 die"), "and can be attacked")

            # Move by Night
            finish_conquest()
            act(base, session, csrf, game_id, "done_attack")
            await asyncio.sleep(1.5)
            require(await tile_button("p07"), "Move by Night has a Play button when fortifying")
            check(state_now()["flags"]["extraFortify"] == 3, "Move by Night gives 3 extra redeployments")

            # Ending the turn draws Appoint a Second Leader (a Leader took a site)
            require(await click_button("#game-actions", "End turn"), "the turn can be ended")
            st = state_now()
            require((st.get("legal") or {}).get("freeLeaderPending"),
                    "Appoint a Second Leader waits for a territory" +
                    ("" if (st.get("legal") or {}).get("freeLeaderPending")
                     else ": " + " | ".join(e["text"] for e in st["log"][-5:])))
            hint = await page.js("document.getElementById('game-actions').textContent")
            check("Appoint a Second Leader" in hint, "the action panel asks where the new Leader goes")
            spot = st["legal"]["leaderPlaceable"][0]
            await click_territory(spot)
            st = state_now()
            check(st["territories"][spot]["leader"] == me + 1 and st["players"][me]["leaders"] == 2,
                  "the second Leader is on the board")
            if st["current"] == me:
                await click_button("#game-actions", "End turn")
            check(state_now()["current"] != me, "the turn passes after the event")
    finally:
        if chrome is not None:
            chrome.terminate()
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except Exception:
                server.kill()
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    chrome_bin = find_chrome()
    try:
        import websockets
    except ImportError:
        websockets = None
    if chrome_bin is None or websockets is None or not os.path.exists(os.path.join(PACK, "map.json")):
        print("SKIP: printed-card browser test (needs Chrome, python websockets and res/maps/lotr)")
        return 0
    try:
        asyncio.run(run(chrome_bin, websockets))
    except Abort:
        pass
    except Exception as error:
        import traceback
        traceback.print_exc()
        check(False, f"printed-card browser test aborted: {error!r}")
    failed = [message for ok, message in RESULTS if not ok]
    if failed:
        print(f"{len(failed)} printed-card browser check(s) failed.")
        return 1
    print("All printed-card browser tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
