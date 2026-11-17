#!/usr/bin/env python3
"""Map input through a real browser with trusted mouse/touch/keyboard events.

Synthetic element.click() calls bypass pointer capture and hit testing, so
they missed that mouse clicks on territories never arrived.  This test drives
headless Chrome over the DevTools protocol (Input.dispatch*Event) against an
isolated server on the generated tiny pack, whose territories are small anchor
hexes like the LOTR pack's:

* a mouse click beside a territory's hex (inside its hit area) selects it and
  a placement from the slider/quick buttons reaches the server;
* a mouse drag pans the map without selecting anything;
* a double click zooms in;
* a touch tap selects a territory and the confirm button places armies;
* a two-finger pinch zooms without selecting anything;
* the dice overlay does not block map clicks.

Skips (exit 0) when no Chrome/Chromium binary or Python `websockets` module is
available.  FRISK_CHROME overrides the browser binary.
"""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from long_game import FRISK_DIR, form, get, make_invite, register, start_server  # noqa: E402

PORT = int(os.environ.get("FRISK_BROWSER_PORT", "18190"))
RESULTS = []


def find_chrome():
    candidates = [os.environ.get("FRISK_CHROME", ""), "google-chrome",
                  "google-chrome-stable", "chromium", "chromium-browser"]
    for name in candidates:
        if name and shutil.which(name):
            return shutil.which(name)
    return None


class Abort(Exception):
    """A step failed that later steps depend on."""


def check(ok, message):
    RESULTS.append((ok, message))
    print(("PASS: " if ok else "FAIL: ") + message, flush=True)
    return ok


def require(ok, message):
    if not check(ok, message):
        raise Abort(message)


class Page:
    def __init__(self, ws):
        self.ws = ws
        self.seq = 0

    async def call(self, method, **params):
        self.seq += 1
        mid = self.seq
        await self.ws.send(json.dumps({"id": mid, "method": method, "params": params}))
        while True:
            message = json.loads(await self.ws.recv())
            if message.get("id") == mid:
                if "error" in message:
                    raise RuntimeError(f"{method}: {message['error']}")
                return message.get("result", {})

    async def js(self, expression):
        result = await self.call("Runtime.evaluate", expression=expression,
                                 awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in result:
            raise RuntimeError(f"{expression[:60]}: {result['exceptionDetails']}")
        return result["result"].get("value")

    async def wait(self, expression, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if await self.js(expression):
                return True
            await asyncio.sleep(0.05)
        return False

    async def mouse(self, kind, x, y, buttons=0, count=1):
        await self.call("Input.dispatchMouseEvent", type=kind, x=x, y=y,
                        button="left" if kind != "mouseMoved" else ("left" if buttons else "none"),
                        buttons=buttons, clickCount=count)

    async def click(self, x, y, count=1):
        await self.mouse("mouseMoved", x, y)
        await self.mouse("mousePressed", x, y, buttons=1, count=count)
        await self.mouse("mouseReleased", x, y, count=count)

    async def touch(self, kind, points):
        await self.call("Input.dispatchTouchEvent", type=kind,
                        touchPoints=[{"x": x, "y": y, "id": i} for i, (x, y) in enumerate(points)])

    async def tap(self, x, y):
        await self.touch("touchStart", [(x, y)])
        await self.touch("touchEnd", [])


def label_point(index, dx=0, dy=0):
    """Screen position of a territory label (map coordinates + offset)."""
    return f"""(() => {{
        const svg = document.querySelector('#map-stage svg');
        const hex = document.getElementById('t{index}');
        const box = hex.getBBox();
        const point = svg.createSVGPoint();
        point.x = box.x + box.width / 2 + {dx};
        point.y = box.y + box.height / 2 + {dy};
        const screen = point.matrixTransform(hex.getScreenCTM());
        return [screen.x, screen.y];
    }})()"""


async def element_center(page, selector):
    return await page.js(f"""(() => {{ const e = document.querySelector({json.dumps(selector)});
        if (!e) return null; const r = e.getBoundingClientRect();
        return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")


async def button_center(page, text):
    return await page.js(f"""(() => {{
        const b = [...document.querySelectorAll('#game-actions button')]
            .find((item) => item.textContent === {json.dumps(text)});
        if (!b) return null; const r = b.getBoundingClientRect();
        return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")


def armies(session, base, game_id, territory):
    return get(session, base + "/api/game/state", {"g": game_id})["territories"][territory]["armies"]


async def run(chrome_bin, websockets):
    tmp = tempfile.mkdtemp(prefix="frisk-browser-")
    server = chrome = None
    try:
        res = os.path.join(tmp, "res")
        os.makedirs(res, exist_ok=True)
        for item in ("index.html", "js", "css"):
            source = os.path.join(FRISK_DIR, "res", item)
            (shutil.copytree if os.path.isdir(source) else shutil.copy)(
                source, os.path.join(res, item))
        server, base = start_server(PORT, tmp)
        admin, admin_csrf = register(base, "admin")
        bob, bob_csrf = register(base, "bob", make_invite(base, admin, admin_csrf))
        game_id = form(admin, base + "/api/game/create", {
            "csrf": admin_csrf, "name": "Input", "map": "tiny", "seats": 2,
            "mode": "deal", "batch": 5})["id"]
        form(bob, base + "/api/game/join", {"csrf": bob_csrf, "g": game_id})
        form(admin, base + "/api/game/start", {"csrf": admin_csrf, "g": game_id})
        state = get(admin, base + "/api/game/state", {"g": game_id})
        player = state["players"][state["current"]]["user"]
        session, csrf = (admin, admin_csrf) if player == "admin" else (bob, bob_csrf)

        def mark_seen():
            # Without this the page opens with a "while you were away" replay,
            # which disables map input until it finishes.
            form(session, base + "/api/game/act", {"csrf": csrf, "g": game_id, "type": "seen"})

        profile = os.path.join(tmp, "chrome")
        chrome = subprocess.Popen([
            chrome_bin, "--headless=new", "--remote-debugging-port=0",
            f"--user-data-dir={profile}", "--no-first-run", "--no-default-browser-check",
            "--window-size=1200,800", "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        port_file = os.path.join(profile, "DevToolsActivePort")
        for _ in range(200):
            if os.path.exists(port_file) and os.path.getsize(port_file) > 0:
                break
            time.sleep(0.05)
        cdp_port = int(open(port_file).read().split()[0])
        import urllib.request
        for _ in range(100):
            targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{cdp_port}/json/list"))
            pages = [t for t in targets if t["type"] == "page"]
            if pages:
                break
            time.sleep(0.05)
        async with websockets.connect(pages[0]["webSocketDebuggerUrl"], max_size=2 ** 26) as ws:
            page = Page(ws)
            await page.call("Page.enable")
            await page.call("Page.navigate", url=base + "/")
            await page.wait("document.readyState === 'complete'")
            await page.js(f"""fetch('/api/login', {{method: 'POST', body: (() => {{
                const f = new FormData(); f.append('name', {json.dumps(player)});
                f.append('password', 'password123'); return f; }})()}}).then(r => r.json())""")
            await page.js("localStorage.setItem('friskMusic', 'off')")
            game_ready = ("document.getElementById('replay-bar').classList.contains('hidden') && "
                          "document.querySelectorAll('#hit-areas .hit.selectable').length > 0")

            # --- mouse ---------------------------------------------------
            mark_seen()
            await page.call("Page.navigate", url=base + f"/?mouse#/game/{game_id}")
            require(await page.wait(game_ready, 15), "board loads with clickable hit areas")
            check(await page.js("getComputedStyle(document.getElementById('dice-overlay')).pointerEvents") == "none",
                  "the dice overlay lets clicks through to the map")
            await asyncio.sleep(0.3)
            mine = [i for i, t in enumerate(state["territories"])
                    if state["players"][t["owner"]]["user"] == player] if state["territories"] else []
            first, second = mine[0], mine[1]
            x, y = await page.js(label_point(first, dx=30))
            hit = await page.js(f"document.elementFromPoint({x}, {y}).getAttribute('class')")
            check(hit == "hit selectable", f"30 map px beside the hex is the territory's hit area ({hit})")
            before = armies(session, base, game_id, first)
            await page.click(x, y)
            require(await page.wait("!!document.querySelector('#game-actions .count-picker')", 3),
                    "a mouse click beside a territory opens the placement controls")
            one = await button_center(page, "1")
            await page.click(*one)
            await asyncio.sleep(0.8)
            check(armies(session, base, game_id, first) == before + 1,
                  "the mouse placement reached the server")

            transform = await page.js("document.getElementById('map-stage').style.transform")
            x, y = await page.js(label_point(second))
            await page.mouse("mouseMoved", x, y)
            await page.mouse("mousePressed", x, y, buttons=1)
            for step in range(1, 9):
                await page.mouse("mouseMoved", x + step * 12, y + step * 6, buttons=1)
            await page.mouse("mouseReleased", x + 96, y + 48)
            await asyncio.sleep(0.3)
            moved = await page.js("document.getElementById('map-stage').style.transform")
            picked = await page.js("!!document.querySelector('#game-actions .count-picker')")
            check(moved != transform and not picked, "a mouse drag pans without selecting")

            scale = await page.js("Number(/scale\\(([0-9.]+)\\)/.exec(document.getElementById('map-stage').style.transform)[1])")
            x, y = await page.js(label_point(second))
            await page.click(x, y)
            await page.click(x, y, count=2)
            await asyncio.sleep(0.3)
            zoomed = await page.js("Number(/scale\\(([0-9.]+)\\)/.exec(document.getElementById('map-stage').style.transform)[1])")
            check(zoomed > scale * 1.5, f"a double click zooms in ({scale:.2f} -> {zoomed:.2f})")

            # --- touch -----------------------------------------------------
            await page.call("Emulation.setDeviceMetricsOverride", width=390, height=844,
                            deviceScaleFactor=1, mobile=True)
            await page.call("Emulation.setTouchEmulationEnabled", enabled=True)
            mark_seen()
            await page.call("Page.navigate", url=base + f"/?touch#/game/{game_id}")
            require(await page.wait(game_ready, 15), "board loads on a phone viewport")
            await asyncio.sleep(0.3)
            remaining = get(session, base + "/api/game/state", {"g": game_id})["legal"]["batchRemaining"]
            x, y = await page.js(label_point(second, dy=-25))
            before = armies(session, base, game_id, second)
            await page.tap(x, y)
            require(await page.wait("!!document.querySelector('#game-actions .count-picker')", 3),
                    "a touch tap beside a territory opens the placement controls")
            # Step the slider down by one with the keyboard, then confirm.
            await page.js("document.querySelector('#game-actions .count-picker input').focus()")
            await page.call("Input.dispatchKeyEvent", type="keyDown", key="ArrowLeft",
                            code="ArrowLeft", windowsVirtualKeyCode=37)
            await page.call("Input.dispatchKeyEvent", type="keyUp", key="ArrowLeft",
                            code="ArrowLeft", windowsVirtualKeyCode=37)
            label = await page.js("document.querySelector('#game-actions .count-picker button').textContent")
            check(label == f"Place {remaining - 1}", f"the slider sets the count ({label})")
            confirm = await element_center(page, "#game-actions .count-picker button")
            await page.tap(*confirm)
            await asyncio.sleep(0.8)
            check(armies(session, base, game_id, second) == before + remaining - 1,
                  "the touch placement reached the server")

            scale = await page.js("Number(/scale\\(([0-9.]+)\\)/.exec(document.getElementById('map-stage').style.transform)[1])")
            cx, cy = await element_center(page, "#map-container")
            await page.touch("touchStart", [(cx - 20, cy), (cx + 20, cy)])
            for step in range(1, 9):
                await page.touch("touchMove", [(cx - 20 - step * 10, cy), (cx + 20 + step * 10, cy)])
            await page.touch("touchEnd", [])
            await asyncio.sleep(0.3)
            pinched = await page.js("Number(/scale\\(([0-9.]+)\\)/.exec(document.getElementById('map-stage').style.transform)[1])")
            picked = await page.js("!!document.querySelector('#game-actions .count-picker')")
            check(pinched > scale * 1.5 and not picked,
                  f"a pinch zooms without selecting ({scale:.2f} -> {pinched:.2f})")
    finally:
        if chrome is not None:
            chrome.terminate()
            try:
                chrome.wait(timeout=10)
            except Exception:
                chrome.kill()
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
    if chrome_bin is None or websockets is None:
        print("SKIP: browser input test (needs Chrome/Chromium and python websockets)")
        return 0
    try:
        asyncio.run(run(chrome_bin, websockets))
    except Abort:
        pass
    except Exception as error:  # report, do not hide, an unexpected crash
        check(False, f"browser input test aborted: {error!r}")
    failed = [message for ok, message in RESULTS if not ok]
    if failed:
        print(f"{len(failed)} browser input check(s) failed.")
        return 1
    print("All browser input tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
